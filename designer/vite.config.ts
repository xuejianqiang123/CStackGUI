import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

/* 打包成 Electron 桌面应用：产物用相对路径引用，file:// 下才能加载 */
export default defineConfig({
  plugins: [react()],
  base: "./",
  server: { port: 5273, strictPort: true },
  build: { outDir: "dist", emptyOutDir: true, target: "chrome120" },
});
