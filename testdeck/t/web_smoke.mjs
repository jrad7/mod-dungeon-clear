#!/usr/bin/env node
/* Smoke the COMMITTED build output: dist/index.html must exist and every
 * asset it references must be present. Catches the classic mistake of
 * committing source changes without rebuilding dist. No server needed. */

import { readFileSync, existsSync, statSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const dist = join(root, "dist");
let failures = 0;

function check(cond, what) {
  console.log(`${cond ? "ok" : "FAIL"}  ${what}`);
  if (!cond) failures++;
}

check(existsSync(join(dist, "index.html")), "dist/index.html exists");

const html = readFileSync(join(dist, "index.html"), "utf-8");
check(html.includes("<title>DC Test Deck</title>"), "title present");
check(html.includes('<div id="root">'), "react mount point present");

const refs = [...html.matchAll(/(?:src|href)="\/([^"]+)"/g)].map((m) => m[1]);
check(refs.length >= 2, `index.html references assets (${refs.length})`);
for (const ref of refs) {
  check(existsSync(join(dist, ref)), `asset exists: ${ref}`);
}

/* Source newer than the build is the "forgot to rebuild" smell. Warning
 * only — mtimes lie across git checkouts. */
const built = statSync(join(dist, "index.html")).mtimeMs;
const srcDir = join(root, "web", "src");
if (existsSync(srcDir)) {
  const { execSync } = await import("node:child_process");
  const newest = execSync(
    `find ${JSON.stringify(srcDir)} -name '*.ts*' -newer ${JSON.stringify(join(dist, "index.html"))} | head -3`,
    { encoding: "utf-8" },
  ).trim();
  if (newest)
    console.log(`warn  source newer than dist (rebuild?):\n${newest}`);
}

process.exit(failures ? 1 : 0);
