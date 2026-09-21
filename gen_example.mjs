import { generateC } from "./designer/dist_codegen.mjs";
import { readFileSync, writeFileSync } from "fs";
import { join } from "path";

const dir = "examples/echarts_demo";
const base = "echarts_demo";
const doc = JSON.parse(readFileSync(join(dir, `${base}.cgui`), "utf8"));
const files = generateC(doc, base);
for (const f of files) {
  if (f.name === "build.bat") continue; // keep our asset-copy build-app.bat
  writeFileSync(join(dir, f.name), f.content);
  console.log("wrote", f.name);
}
