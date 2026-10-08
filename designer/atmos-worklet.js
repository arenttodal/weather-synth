// Runs Atmospheric's C++ engine (atmos.wasm) on the audio thread.
// Messages in: {type:'init', bytes} | {type:'call', fn, args} | {type:'batch', calls:[[fn,...args]]}
// Messages out: {type:'ready', schema} | {type:'error', message} | {type:'meter', peak, voices} | {type:'result', id, value}
class AtmosProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.e = null;
    this.frames = 0;
    this.peak = 0;
    this.port.onmessage = (ev) => this.onMessage(ev.data);
  }

  async onMessage(m) {
    try {
      if (m.type === "init") {
        const noop = () => 0;
        const wasi = new Proxy({}, { get: () => noop });
        const { instance } = await WebAssembly.instantiate(m.bytes, { wasi_snapshot_preview1: wasi });
        this.e = instance.exports;
        this.e._initialize();
        this.e.atmos_init(sampleRate);
        this.port.postMessage({ type: "ready", schema: this.readString(this.e.atmos_schema()), sampleRate });
      } else if (!this.e) {
        return;
      } else if (m.type === "call") {
        const v = this.e[m.fn](...(m.args || []));
        if (m.id !== undefined) this.port.postMessage({ type: "result", id: m.id, value: v });
      } else if (m.type === "batch") {
        let last;
        for (const [fn, ...args] of m.calls) last = this.e[fn](...args);
        if (m.id !== undefined) this.port.postMessage({ type: "result", id: m.id, value: last });
      } else if (m.type === "patch") {
        const n = this.e.atmos_param_count();
        const v = Array.from(new Float64Array(this.e.memory.buffer, this.e.atmos_patch(), n));
        this.port.postMessage({ type: "result", id: m.id, value: v });
      } else if (m.type === "forces") {
        const p = this.e.atmos_forces();
        const f = Array.from(new Float64Array(this.e.memory.buffer, p, 12));
        this.port.postMessage({ type: "result", id: m.id, value: f });
      }
    } catch (err) {
      this.port.postMessage({ type: "error", message: String(err && err.message ? err.message : err) });
    }
  }

  readString(ptr) {
    const mem = new Uint8Array(this.e.memory.buffer);
    let end = ptr;
    while (mem[end]) end++;
    let s = "";
    for (let i = ptr; i < end; i++) s += String.fromCharCode(mem[i]);
    return s;
  }

  process(_inputs, outputs) {
    const out = outputs[0];
    if (!this.e || !out || !out[0]) return true;
    const n = out[0].length;
    this.e.atmos_render(n);
    const L = new Float32Array(this.e.memory.buffer, this.e.atmos_left(), n);
    const R = new Float32Array(this.e.memory.buffer, this.e.atmos_right(), n);
    out[0].set(L);
    if (out[1]) out[1].set(R);
    for (let i = 0; i < n; i++) this.peak = Math.max(this.peak, Math.abs(L[i]), Math.abs(R[i]));
    this.frames += n;
    if (this.frames >= sampleRate / 20) {
      this.port.postMessage({ type: "meter", peak: this.peak, voices: this.e.atmos_active_voices() });
      this.frames = 0;
      this.peak = 0;
    }
    return true;
  }
}
registerProcessor("atmos-processor", AtmosProcessor);
