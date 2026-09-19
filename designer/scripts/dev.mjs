/* dev.mjs -- 一条命令起开发环境：先跑 vite，端口通了再起 electron。
 * 不引入 concurrently / wait-on 这类依赖，少装几个包就少几处失败。
 */
import { spawn } from "node:child_process";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, "..");
const PORT = 5273;
const URL = `http://localhost:${PORT}`;

const npx = process.platform === "win32" ? "npx.cmd" : "npx";
const electron = process.platform === "win32" ? "electron.cmd" : "electron";

const vite = spawn(npx, ["vite", "--port", String(PORT), "--strictPort"], {
  cwd: root,
  stdio: "inherit",
  shell: process.platform === "win32",
});

const waitPort = (tries = 80) =>
  new Promise((resolve, reject) => {
    const probe = (left) => {
      if (left <= 0) return reject(new Error("等待 vite 端口超时"));
      const sock = net.connect(PORT, "127.0.0.1");
      sock.once("connect", () => {
        sock.destroy();
        resolve();
      });
      sock.once("error", () => {
        sock.destroy();
        setTimeout(() => probe(left - 1), 250);
      });
    };
    probe(tries);
  });

let child = null;
try {
  await waitPort();
  console.log(`[cstackgui] vite ready at ${URL}, starting electron...`);
  child = spawn(electron, ["."], {
    cwd: root,
    stdio: "inherit",
    shell: process.platform === "win32",
    env: { ...process.env, CSTACKGUI_DEV_URL: URL },
  });
  child.on("exit", (code) => {
    vite.kill();
    process.exit(code ?? 0);
  });
} catch (e) {
  console.error("[cstackgui] dev 启动失败:", e.message);
  vite.kill();
  process.exit(1);
}

process.on("SIGINT", () => {
  child?.kill();
  vite.kill();
  process.exit(0);
});
