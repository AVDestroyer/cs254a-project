.DEFAULT_GOAL := cpu

PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ALGORITHMS_DIR := $(PROJECT_ROOT)/algorithms
SIMULATOR_DIR := $(PROJECT_ROOT)/simulator
VORTEX_BUILD := $(PROJECT_ROOT)/third_party/vortex/build
VORTEX_MK := $(SIMULATOR_DIR)/vortex.mk

CPU_CXXFLAGS ?= -std=c++17 -O3 -march=native -Wall -Wextra -Wpedantic
export CCACHE_DIR := $(PROJECT_ROOT)/.cache/ccache
export POCL_CACHE_DIR := $(PROJECT_ROOT)/.cache/pocl

QUBITS ?= 15
HADAMARD_QUBITS ?= $(QUBITS)
GROVER_QUBITS ?= 4
GROVER_ITERATIONS ?=
GROVER_OPTS = -n$(GROVER_QUBITS) $(if $(GROVER_ITERATIONS),-i$(GROVER_ITERATIONS))
QFT_QUBITS ?= 4
PERF ?= 1
VORTEX_DEBUG ?=
VORTEX_CORES ?= 16
VORTEX_WARPS ?= 4
VORTEX_THREADS ?= 4
VORTEX_CONFIGS = -DVX_CFG_NUM_CORES=$(VORTEX_CORES) \
	-DVX_CFG_NUM_WARPS=$(VORTEX_WARPS) \
	-DVX_CFG_NUM_THREADS=$(VORTEX_THREADS) \
	-DVX_CFG_L2_ENABLE $(CONFIGS)

HADAMARD_CPU_BINARY := $(PROJECT_ROOT)/build/hadamard-cpu
GROVER_CPU_BINARY := $(PROJECT_ROOT)/build/grover-cpu
QFT_CPU_BINARY := $(PROJECT_ROOT)/build/qft-cpu

.PHONY: cpu cpu-run vortex-build vortex-simx vortex-rtlsim \
	hadamard-cpu hadamard-cpu-run hadamard-build hadamard-simx hadamard-rtlsim \
	grover-cpu grover-cpu-run grover-build grover-simx grover-rtlsim \
	qft-cpu qft-cpu-run qft-build qft-simx qft-rtlsim check-vortex

$(HADAMARD_CPU_BINARY): $(ALGORITHMS_DIR)/hadamard/cpu/main.cpp $(SIMULATOR_DIR)/state.h $(SIMULATOR_DIR)/cpu_ops.h $(SIMULATOR_DIR)/benchmark.h Makefile
	mkdir -p $(dir $@)
	$(CXX) $(CPU_CXXFLAGS) $< -o $@

$(GROVER_CPU_BINARY): $(ALGORITHMS_DIR)/grover/cpu/main.cpp $(ALGORITHMS_DIR)/grover/cpu/reference.h $(SIMULATOR_DIR)/state.h $(SIMULATOR_DIR)/benchmark.h Makefile
	mkdir -p $(dir $@)
	$(CXX) $(CPU_CXXFLAGS) $< -o $@

$(QFT_CPU_BINARY): $(ALGORITHMS_DIR)/qft/cpu/main.cpp $(ALGORITHMS_DIR)/qft/cpu/reference.h $(SIMULATOR_DIR)/state.h $(SIMULATOR_DIR)/cpu_ops.h $(SIMULATOR_DIR)/benchmark.h Makefile
	mkdir -p $(dir $@)
	$(CXX) $(CPU_CXXFLAGS) $< -o $@

hadamard-cpu: $(HADAMARD_CPU_BINARY)
hadamard-cpu-run: hadamard-cpu
	$(HADAMARD_CPU_BINARY) $(HADAMARD_QUBITS)

grover-cpu: $(GROVER_CPU_BINARY)
grover-cpu-run: grover-cpu
	$(GROVER_CPU_BINARY) $(GROVER_OPTS)

qft-cpu: $(QFT_CPU_BINARY)
qft-cpu-run: qft-cpu
	$(QFT_CPU_BINARY) $(QFT_QUBITS)

check-vortex:
	@mkdir -p $(CCACHE_DIR) $(POCL_CACHE_DIR)
	@test -f $(VORTEX_BUILD)/config.mk || \
		(echo "Vortex is not configured; run scripts/setup-vortex.sh first"; exit 1)

hadamard-build hadamard-simx hadamard-rtlsim: ALGORITHM = hadamard
hadamard-build hadamard-simx hadamard-rtlsim: ALGORITHM_OPTS = -n$(HADAMARD_QUBITS)
grover-build grover-simx grover-rtlsim: ALGORITHM = grover
grover-build grover-simx grover-rtlsim: ALGORITHM_OPTS = $(GROVER_OPTS)
qft-build qft-simx qft-rtlsim: ALGORITHM = qft
qft-build qft-simx qft-rtlsim: ALGORITHM_OPTS = -n$(QFT_QUBITS)

hadamard-build grover-build qft-build: VORTEX_TARGET = all
hadamard-simx grover-simx qft-simx: VORTEX_TARGET = run-simx
hadamard-rtlsim grover-rtlsim qft-rtlsim: VORTEX_TARGET = run-rtlsim

hadamard-build hadamard-simx hadamard-rtlsim \
grover-build grover-simx grover-rtlsim \
qft-build qft-simx qft-rtlsim: check-vortex
	mkdir -p $(VORTEX_BUILD)/tests/regression/$(ALGORITHM)
	$(MAKE) -C $(VORTEX_BUILD)/tests/regression/$(ALGORITHM) -f $(VORTEX_MK) \
		ALGORITHM=$(ALGORITHM) OPTS="$(ALGORITHM_OPTS)" PERF=$(PERF) \
		DEBUG=$(VORTEX_DEBUG) CONFIGS="$(VORTEX_CONFIGS)" $(VORTEX_TARGET)

# Keep the original top-level commands working for existing scripts.
cpu: hadamard-cpu
cpu-run: hadamard-cpu-run
vortex-build: hadamard-build
vortex-simx: hadamard-simx
vortex-rtlsim: hadamard-rtlsim
