// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatUpdateCheck.h"

#include "HapbeatEditorTools.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "HapbeatUpdateCheck"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatUpdate, Log, All);

bool FHapbeatUpdateCheck::bNotifiedThisSession = false;

namespace
{
	const TCHAR* FeedUrl = TEXT("https://devtools.hapbeat.com/releases.json");
	const TCHAR* ProductId = TEXT("unreal-sdk");
	const TCHAR* PluginName = TEXT("HapbeatSDK");

	const TCHAR* ConfigSection = TEXT("Hapbeat.UpdateCheck");
	const TCHAR* KeyEnabled = TEXT("bAutoCheckEnabled");
	const TCHAR* KeyLastCheck = TEXT("LastCheckUtc");
	const TCHAR* KeyCachedLatest = TEXT("CachedLatest");

	/** Network is hit at most this often; the notice itself is once per session. */
	constexpr double FetchIntervalHours = 24.0;
	constexpr float RequestTimeoutSeconds = 5.0f;

	/**
	 * "v0.3.1" / "0.3.1-rc1" / "0.1.2d4" -> {0,3,1} / {0,3,1} / {0,1,2}.
	 * Returns false for anything it cannot read, which makes the caller stay
	 * quiet -- a bad parse must never produce a bogus "update available".
	 */
	bool ParseVersion(const FString& Version, TArray<int32>& OutParts)
	{
		FString Trimmed = Version.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return false;
		}
		if (Trimmed[0] == TEXT('v') || Trimmed[0] == TEXT('V'))
		{
			Trimmed.RightChopInline(1);
		}

		int32 Cut = INDEX_NONE;
		int32 DashAt = INDEX_NONE;
		int32 PlusAt = INDEX_NONE;
		Trimmed.FindChar(TEXT('-'), DashAt);
		Trimmed.FindChar(TEXT('+'), PlusAt);
		Cut = (DashAt == INDEX_NONE) ? PlusAt : (PlusAt == INDEX_NONE ? DashAt : FMath::Min(DashAt, PlusAt));
		if (Cut != INDEX_NONE)
		{
			Trimmed.LeftInline(Cut);
		}

		TArray<FString> Parts;
		Trimmed.ParseIntoArray(Parts, TEXT("."), /*InCullEmpty=*/false);
		if (Parts.Num() == 0)
		{
			return false;
		}

		OutParts.Reset();
		for (const FString& Part : Parts)
		{
			// Stop at the first non-digit so a dev suffix like "2d4" reads as 2.
			int32 End = 0;
			while (End < Part.Len() && FChar::IsDigit(Part[End]))
			{
				++End;
			}
			if (End == 0)
			{
				return false;
			}
			OutParts.Add(FCString::Atoi(*Part.Left(End)));
		}
		return true;
	}

	bool IsNewer(const FString& Candidate, const FString& Baseline)
	{
		TArray<int32> A;
		TArray<int32> B;
		if (!ParseVersion(Candidate, A) || !ParseVersion(Baseline, B))
		{
			return false;
		}
		const int32 Count = FMath::Max(A.Num(), B.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 X = A.IsValidIndex(Index) ? A[Index] : 0;
			const int32 Y = B.IsValidIndex(Index) ? B[Index] : 0;
			if (X != Y)
			{
				return X > Y;
			}
		}
		return false;
	}

	/**
	 * Pulls one product's "latest" out of the feed.
	 *
	 * The feed keys products dynamically, and each product entry is flat (see
	 * release-feed.schema.json), so slicing out the entry's braces and reading
	 * "latest" from inside is sufficient and avoids a full JSON parse.
	 */
	bool ParseLatest(const FString& Json, const FString& Product, FString& OutLatest)
	{
		const FString Key = FString::Printf(TEXT("\"%s\""), *Product);
		int32 KeyAt = Json.Find(Key, ESearchCase::CaseSensitive);
		if (KeyAt == INDEX_NONE)
		{
			return false;
		}
		int32 OpenAt = Json.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, KeyAt);
		int32 CloseAt = Json.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenAt);
		if (OpenAt == INDEX_NONE || CloseAt == INDEX_NONE)
		{
			return false;
		}

		const FString Entry = Json.Mid(OpenAt, CloseAt - OpenAt);
		const FString LatestKey = TEXT("\"latest\"");
		int32 LatestAt = Entry.Find(LatestKey, ESearchCase::CaseSensitive);
		if (LatestAt == INDEX_NONE)
		{
			return false;
		}
		int32 FirstQuote = Entry.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, LatestAt + LatestKey.Len());
		if (FirstQuote == INDEX_NONE)
		{
			return false;
		}
		int32 SecondQuote = Entry.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstQuote + 1);
		if (SecondQuote == INDEX_NONE)
		{
			return false;
		}
		OutLatest = Entry.Mid(FirstQuote + 1, SecondQuote - FirstQuote - 1);
		return !OutLatest.IsEmpty();
	}

	/**
	 * True when the plugin is a working tree rather than an installed copy.
	 * Telling a developer to update the SDK they are editing is noise.
	 */
	bool IsDeveloperCheckout()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		if (!Plugin.IsValid())
		{
			return false;
		}
		const FString GitDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT(".git"));
		return FPaths::DirectoryExists(GitDir) || FPaths::FileExists(GitDir);
	}
}

FString FHapbeatUpdateCheck::GetInstalledVersion()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
	return Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString();
}

bool FHapbeatUpdateCheck::IsAutoCheckEnabled()
{
	bool bEnabled = true;
	GConfig->GetBool(ConfigSection, KeyEnabled, bEnabled, GEditorPerProjectIni);
	return bEnabled;
}

void FHapbeatUpdateCheck::SetAutoCheckEnabled(bool bEnabled)
{
	GConfig->SetBool(ConfigSection, KeyEnabled, bEnabled, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void FHapbeatUpdateCheck::Register()
{
	CheckIfDue(/*bUserInitiated=*/false);

	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = ToolsMenu->FindOrAddSection("Hapbeat", LOCTEXT("HapbeatSection", "Hapbeat"));

	Section.AddMenuEntry("HapbeatCheckForUpdates",
		LOCTEXT("CheckNow", "Check for SDK Updates"),
		LOCTEXT("CheckNowTooltip", "Query the Hapbeat release feed now and report the result, even when already up to date."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FHapbeatUpdateCheck::CheckNow)));

	Section.AddMenuEntry("HapbeatCheckForUpdatesOnStartup",
		LOCTEXT("CheckOnStartup", "Check for SDK Updates on Startup"),
		LOCTEXT("CheckOnStartupTooltip", "Check the release feed when the editor opens. The notice appears at most once per session."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([] { SetAutoCheckEnabled(!IsAutoCheckEnabled()); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([] { return IsAutoCheckEnabled(); })),
		EUserInterfaceActionType::ToggleButton);

	FHapbeatEditorTools::RegisterMenus(Section);
}

void FHapbeatUpdateCheck::Unregister()
{
	if (UObjectInitialized())
	{
		UToolMenus::UnRegisterStartupCallback(nullptr);
	}
}

void FHapbeatUpdateCheck::CheckNow()
{
	// An explicit request always talks to the feed and always answers, ignoring
	// both the fetch interval and the once-per-session rule.
	CheckIfDue(/*bUserInitiated=*/true);
}

void FHapbeatUpdateCheck::CheckIfDue(bool bUserInitiated)
{
	if (!bUserInitiated)
	{
		if (!IsAutoCheckEnabled() || bNotifiedThisSession || IsDeveloperCheckout())
		{
			return;
		}

		// Serve from the cached answer inside the fetch window. Fetch frequency
		// and notice frequency are deliberately separate: the network is touched
		// at most daily, but the notice can still appear on every editor start.
		FString LastCheckRaw;
		GConfig->GetString(ConfigSection, KeyLastCheck, LastCheckRaw, GEditorPerProjectIni);
		FDateTime LastCheck;
		if (FDateTime::Parse(LastCheckRaw, LastCheck) &&
			(FDateTime::UtcNow() - LastCheck).GetTotalHours() < FetchIntervalHours)
		{
			FString Cached;
			GConfig->GetString(ConfigSection, KeyCachedLatest, Cached, GEditorPerProjectIni);
			if (!Cached.IsEmpty())
			{
				HandleFeedResponse(FString::Printf(TEXT("{\"products\":{\"%s\":{\"latest\":\"%s\"}}}"), ProductId, *Cached), false);
			}
			return;
		}
	}

	// Stamp before sending, not after: an offline editor must not retry the
	// timeout on every startup.
	GConfig->SetString(ConfigSection, KeyLastCheck, *FDateTime::UtcNow().ToIso8601(), GEditorPerProjectIni);

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(FeedUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->OnProcessRequestComplete().BindLambda(
		[bUserInitiated](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
		{
			if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode() != 200)
			{
				if (bUserInitiated)
				{
					UE_LOG(LogHapbeatUpdate, Warning, TEXT("[Hapbeat] Could not reach the release feed."));
				}
				return; // Automatic checks stay silent on failure.
			}
			HandleFeedResponse(Response->GetContentAsString(), bUserInitiated);
		});
	Request->ProcessRequest();
}

void FHapbeatUpdateCheck::HandleFeedResponse(const FString& Payload, bool bUserInitiated)
{
	const FString Current = GetInstalledVersion();
	FString Latest;
	if (Current.IsEmpty() || !ParseLatest(Payload, ProductId, Latest))
	{
		if (bUserInitiated)
		{
			UE_LOG(LogHapbeatUpdate, Warning, TEXT("[Hapbeat] The release feed did not list '%s'."), ProductId);
		}
		return;
	}

	GConfig->SetString(ConfigSection, KeyCachedLatest, *Latest, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);

	if (!IsNewer(Latest, Current))
	{
		if (bUserInitiated)
		{
			UE_LOG(LogHapbeatUpdate, Log, TEXT("[Hapbeat] Unreal SDK v%s is up to date."), *Current);
		}
		return;
	}

	if (!bUserInitiated && bNotifiedThisSession)
	{
		return;
	}
	bNotifiedThisSession = true;

	UE_LOG(LogHapbeatUpdate, Log,
		TEXT("[Hapbeat] Unreal SDK v%s is available (using v%s).\n")
		TEXT("  Changelog: https://devtools.hapbeat.com/docs/sdk-integration/unreal-sdk/changelog/\n")
		TEXT("  (Shown once per editor session. Turn it off via Tools > Check for SDK Updates on Startup.)"),
		*Latest, *Current);
}

#undef LOCTEXT_NAMESPACE
