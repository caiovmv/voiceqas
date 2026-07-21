const fs = require("fs");
const path = require("path");
const p = path.join(__dirname, "cc-grpc-native-e2e.mjs");
let s = fs.readFileSync(p, "utf8");
s = s.replace(
  "processingMs: body.processing_ms ?? null, durationMs: body.duration_ms ?? null",
  "processingMs: body.processing_ms ?? body.processingMs ?? null, durationMs: body.duration_ms ?? body.durationMs ?? null",
);
s = s.replace(
  "if (typeof body.processing_ms !== 'number' && !body.model)",
  "if (typeof body.processing_ms !== 'number' && typeof body.processingMs !== 'number' && !body.model)",
);
fs.writeFileSync(p, s, "utf8");
console.log("fixed processing_ms parse");