# --- toolchain ---
EMCC ?= emcc

# --- paths ---
ENGINE_SRC := engine/src/sv_engine.c
ENGINE_INC := engine/include
OUT_DIR    := web/public
OUT_JS     := $(OUT_DIR)/sv_engine.js

# --- emscripten flags ---
EMFLAGS := -O0 -g \
  -sWASM=1 \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sENVIRONMENT=web \
  -sEXPORTED_FUNCTIONS="['_simulate','_malloc','_free']" \
  -sEXPORTED_RUNTIME_METHODS="['cwrap','ccall']" \
  --no-entry

.PHONY: web clean

# build wasm + js into web/public/
web: $(OUT_JS)

$(OUT_JS): $(ENGINE_SRC) $(wildcard $(ENGINE_INC)/*.h)
	@mkdir -p $(OUT_DIR)
	$(EMCC) $(ENGINE_SRC) -I$(ENGINE_INC) $(EMFLAGS) -o $(OUT_JS)

clean:
	rm -f $(OUT_DIR)/sv_engine.js $(OUT_DIR)/sv_engine.wasm
