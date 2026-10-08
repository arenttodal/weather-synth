import http from "node:http";
import { createApp } from "./app.js";

const port = parseInt(process.env.PORT || "8080", 10);
const handle = createApp({
  owmKey: process.env.OWM_API_KEY || "",
  ipinfoToken: process.env.IPINFO_TOKEN || "",
  log: (...a) => console.log(new Date().toISOString(), ...a),
});

http.createServer(handle).listen(port, () => {
  console.log(`atmospheric relay listening on :${port}`);
  if (!process.env.OWM_API_KEY) console.warn("OWM_API_KEY is not set; /v1/* will answer 503");
});
