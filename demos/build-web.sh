#!/bin/sh
set -e
EMSDK=${EMSDK:-/media/projectos/projects/emsdk}
export PATH="$EMSDK/upstream/emscripten:$PATH"
export EM_CONFIG="$EMSDK/.emscripten"

ROOT=$(cd "$(dirname "$0")/.." && pwd)
GPU="$ROOT/gpu"
DEMOS="$ROOT/demos"
OUT=${1:-"$ROOT/build-web"}
DEMO=${2:-SDLLightsShadows}
mkdir -p "$OUT"

em++ -std=c++14 -O2 \
  -DGPU_HAS_OPENGLES=1 -DGPU_HAS_NULL=1 -DGPU_DEMO_HAS_OPENGLES=1 \
  -DGPU_GLES_HAS_ES31=0 -DGPU_GLES_HAS_ES32=0 \
  -DGPU_GLES_HAS_KHR_DEBUG=0 -DGPU_GLES_HAS_BUFFER_MAP=0 \
  -I"$GPU/include" -I"$DEMOS" \
  "$GPU/src/GPUError.cpp" "$GPU/src/GPUDiagnostics.cpp" \
  "$GPU/src/GPUProfiler.cpp" "$GPU/src/GPUBackend.cpp" \
  "$GPU/backends/null/NullDevice.cpp" "$GPU/backends/gles/GLESDevice.cpp" \
  "$DEMOS/ShadowDemoCommon.cpp" "$DEMOS/SDLGLESSurface.cpp" \
  "$DEMOS/$DEMO.cpp" \
  -sUSE_SDL=2 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
  -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY -sASYNCIFY_STACK_SIZE=16384 \
  -o "$OUT/demo.js"

cat > "$OUT/index.html" <<'HTML'
<!doctype html>
<meta charset="utf-8">
<title>gpu — luzes e sombras (WebGL2)</title>
<style>
  html,body{margin:0;height:100%;background:#111;color:#ddd;
            font:14px system-ui,sans-serif}
  #wrap{display:flex;flex-direction:column;height:100%}
  #canvas{flex:1;display:block;width:100%;background:#000}
  #log{max-height:8em;overflow:auto;padding:.5em;background:#000;
       font:12px ui-monospace,monospace;white-space:pre-wrap}
</style>
<div id="wrap">
  <canvas id="canvas" tabindex="-1"></canvas>
  <pre id="log"></pre>
</div>
<script>
  const logElement = document.getElementById('log');
  const print = text => { logElement.textContent += text + '\n';
                          logElement.scrollTop = logElement.scrollHeight; };
  const query = new URLSearchParams(location.search);
  var Module = {
    canvas: document.getElementById('canvas'),
    arguments: (query.get('args') || '').split(',').filter(a => a.length),
    print: text => { print(text); console.log('[demo] ' + text); },
    printErr: text => { print(text); console.log('[demo] ' + text); },
  };
</script>
<script src="demo.js"></script>
HTML

echo "gerado em $OUT/index.html"
echo "servir com: python3 -m http.server -d $OUT 8080"
