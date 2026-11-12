PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
VORTEX_ROOT := $(PROJECT_ROOT)/third_party/vortex
VORTEX_BUILD := $(VORTEX_ROOT)/build
GPU_SRC := $(PROJECT_ROOT)/GPU-simulations
VORTEX_APP_BUILD := $(VORTEX_BUILD)/tests/regression/quantum-gate
CPU_BINARY := $(PROJECT_ROOT)/build/basic-gates-cpu
CPU_CXXFLAGS ?= -std=c++17 -O3 -march=native -Wall -Wextra -Wpedantic
export CCACHE_DIR := $(PROJECT_ROOT)/.cache/ccache
export POCL_CACHE_DIR := $(PROJECT_ROOT)/.cache/pocl

QUBITS ?= 15
PERF ?= 1
VORTEX_DEBUG ?=
VORTEX_CORES ?= 16
VORTEX_WARPS ?= 4
VORTEX_THREADS ?= 4
VORTEX_CONFIGS = -DVX_CFG_NUM_CORES=$(VORTEX_CORES) \
	-DVX_CFG_NUM_WARPS=$(VORTEX_WARPS) \
	-DVX_CFG_NUM_THREADS=$(VORTEX_THREADS) \
	-DVX_CFG_L2_ENABLE $(CONFIGS)

.PHONY: cpu cpu-run vortex-build vortex-simx vortex-rtlsim check-vortex

cpu: $(CPU_BINARY)

$(CPU_BINARY): $(PROJECT_ROOT)/CPU-simulations/basic-gates.cpp Makefile
	mkdir -p $(dir $@)
	$(CXX) $(CPU_CXXFLAGS) $< -o $@

cpu-run: cpu
	$(CPU_BINARY) $(QUBITS)

check-vortex:
	@mkdir -p $(CCACHE_DIR) $(POCL_CACHE_DIR)
	@test -f $(VORTEX_BUILD)/config.mk || \
		(echo "Vortex is not configured; run scripts/setup-vortex.sh first"; exit 1)

vortex-build: check-vortex
	mkdir -p $(VORTEX_APP_BUILD)
	$(MAKE) -C $(VORTEX_APP_BUILD) -f $(GPU_SRC)/vortex.mk \
		QUANTUM_SRC_DIR=$(GPU_SRC) OPTS="-n$(QUBITS)" PERF=$(PERF) \
		DEBUG=$(VORTEX_DEBUG) CONFIGS="$(VORTEX_CONFIGS)" all

vortex-simx: check-vortex
	mkdir -p $(VORTEX_APP_BUILD)
	$(MAKE) -C $(VORTEX_APP_BUILD) -f $(GPU_SRC)/vortex.mk \
		QUANTUM_SRC_DIR=$(GPU_SRC) OPTS="-n$(QUBITS)" PERF=$(PERF) \
		DEBUG=$(VORTEX_DEBUG) CONFIGS="$(VORTEX_CONFIGS)" run-simx

vortex-rtlsim: check-vortex
	mkdir -p $(VORTEX_APP_BUILD)
	$(MAKE) -C $(VORTEX_APP_BUILD) -f $(GPU_SRC)/vortex.mk \
		QUANTUM_SRC_DIR=$(GPU_SRC) OPTS="-n$(QUBITS)" PERF=$(PERF) \
		DEBUG=$(VORTEX_DEBUG) CONFIGS="$(VORTEX_CONFIGS)" run-rtlsim

