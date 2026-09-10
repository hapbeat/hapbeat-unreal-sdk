// Generate declaration indexes from the SDK's reflected public interface.
// Run: node Scripts/generate_api_reference.mjs [--check]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const check = process.argv.includes('--check');
const modules = ['HapbeatSDK', 'HapbeatSDKSamples'];
const records = [];
// Balanced scan understands quoted strings (metadata contains nested parentheses).
function endPair(s, start, left = '(', right = ')') {
  let depth = 0, quoted = false;
  for (let i = start; i < s.length; i++) {
    if (s[i] === '\\' && quoted) { i++; continue; }
    if (s[i] === '"') quoted = !quoted;
    if (quoted) continue;
    if (s[i] === left) depth++;
    if (s[i] === right && --depth === 0) return i + 1;
  }
  throw new Error('Unbalanced declaration');
}
for (const module of modules) {
  const dir = path.join(root, 'Source', module, 'Public');
  for (const file of fs.readdirSync(dir).filter(x => x.endsWith('.h')).sort()) {
    if (module === 'HapbeatSDKSamples' && !['HapbeatAddressOverridePanelComponent.h', 'HapbeatEventLoggerComponent.h', 'HapbeatStatusOverlayComponent.h'].includes(file)) continue;
    const source = fs.readFileSync(path.join(dir, file), 'utf8');
    // Keep only exposed functions and editor/Blueprint properties, not engine callbacks.
    for (const m of source.matchAll(/\b(UFUNCTION|UPROPERTY)\s*\(/g)) {
      const end = endPair(source, source.indexOf('(', m.index));
      const meta = source.slice(m.index, end);
      if (!/BlueprintCallable|BlueprintPure|BlueprintAssignable|BlueprintRead|EditAnywhere|EditDefaultsOnly|VisibleAnywhere/.test(meta)) continue;
      const prefix = source.slice(0, m.index);
      const owners = [...prefix.matchAll(/(?:class|struct)\s+\w+_API\s+(\w+)/g)];
      const owner = owners.at(-1)?.[1];
      if (!owner) throw new Error(`Missing owner: ${file}`);
      let tail = source.slice(end).trimStart();
      const stop = tail.search(/[;{]/);
      if (stop < 0) throw new Error(`Missing declaration: ${file}`);
      const declaration = tail.slice(0, stop).trim().replace(/\s+/g, ' ') + ';';
      const name = m[1] === 'UFUNCTION'
        ? declaration.match(/(\w+)\s*\(/)?.[1]
        : declaration.split('=')[0].match(/(\w+)\s*;?\s*$/)?.[1];
      if (!name) throw new Error(`Missing name: ${declaration}`);
      const title = meta.match(/DisplayName\s*=\s*"([^"]+)"/)?.[1] ?? name;
      const tooltip = meta.match(/Tooltip\s*=\s*"((?:\\.|[^"\\])*)"/)?.[1];
      records.push({ module, file, owner, name, title, declaration, meta,
        tooltip: tooltip?.replace(/\\n/g, ' ').replace(/\\"/g, '"'), fn: m[1] === 'UFUNCTION' });
    }
  }
}
function update(file, body) {
  const start = '<!-- api-index:start -->', end = '<!-- api-index:end -->';
  const target = path.join(root, 'docs', file);
  const old = fs.readFileSync(target, 'utf8').replace(/\r\n/g, '\n');
  const block = `${start}\n${body.trim()}\n${end}\n`;
  const begin = old.indexOf(start);
  const next = begin < 0 ? old.trimEnd() + '\n\n' + block
    : old.slice(0, begin) + block + old.slice(old.indexOf(end, begin) + end.length).replace(/^\n/, '');
  if (old === next) return;
  if (check) throw new Error(`${file}: API index differs from headers; regenerate it.`);
  fs.writeFileSync(target, next);
}
const functions = records.filter(x => x.fn);
let bp = '## ノードの宣言索引\n\n公開する全 `BlueprintCallable` / `BlueprintPure` 関数です。同名ノードは所有クラスで区別します。引数の型・既定値・戻り値は宣言どおりです。非 const 参照引数は出力ピン、`WorldContextObject` は通常自動入力です。component のメンバー関数はその component を Target に渡します。使いどころと動作上の制約は上の機能別説明を参照してください。\n\n';
for (const r of functions) {
  bp += `### ${r.title} — ${r.owner}.${r.name}\n\n`;
  bp += `所有: \`${r.module} / ${r.file}\`。${/BlueprintPure/.test(r.meta) ? 'Pure（実行ピンなし）' : 'Callable（実行ピンあり）'}。\n\n`;
  bp += '```cpp\n' + r.declaration + '\n```\n\n';
}
update('blueprint-nodes.md', bp);
let cpp = '## 公開プロパティ・関数の宣言索引\n\nゲーム実装から利用する反映対象の宣言（`UFUNCTION` と編集・Blueprint 公開 `UPROPERTY`）をヘッダーから列挙しています。継承したメンバーは基底クラスを参照してください。ネットワーク送信スレッド・パケット組立などの内部 API と Unreal のライフサイクル override は対象外です。通常の C++ 専用 API は前節で説明します。\n\n';
for (const owner of [...new Set(records.map(x => x.owner))]) {
  const members = records.filter(x => x.owner === owner);
  cpp += `### ${owner}\n\nヘッダー: \`Source/${members[0].module}/Public/${members[0].file}\`\n\n`;
  for (const r of members) {
    cpp += `- \`${r.name}\`${r.fn ? '（関数）' : /BlueprintAssignable/.test(r.meta) ? '（イベント）' : '（プロパティ）'}\n\n`;
    cpp += '  ```cpp\n  ' + r.declaration + '\n  ```\n\n';
    const limits = [...r.meta.matchAll(/(ClampMin|ClampMax|UIMin|UIMax)\s*=\s*"([^"]+)"/g)].map(x => `${x[1]}=${x[2]}`);
    if (limits.length) cpp += `  Editor 範囲: ${limits.join(', ')}。\n\n`;
    if (!r.fn) cpp += `  ${/BlueprintReadOnly/.test(r.meta) ? 'Blueprint 読取専用。' : /BlueprintReadWrite/.test(r.meta) ? 'Blueprint 読み書き可。' : ''}${/EditAnywhere|EditDefaultsOnly/.test(r.meta) ? 'Editor で設定可能。' : ''}\n\n`;
  }
}
update('cpp-api.md', cpp);
console.log(`API indexes verified: ${functions.length} functions, ${records.length - functions.length} properties, ${new Set(records.map(x => x.owner)).size} classes/structs.`);
