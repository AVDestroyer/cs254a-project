SIMULATOR_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PROJECT_ROOT := $(abspath $(SIMULATOR_DIR)/..)
ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

ifndef ALGORITHM
$(error ALGORITHM must name a directory under algorithms/)
endif

PROJECT := $(ALGORITHM)
ALGORITHM_DIR := $(PROJECT_ROOT)/algorithms/$(ALGORITHM)
SRC_DIR := $(ALGORITHM_DIR)/gpu
SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp
HDRS := $(SRC_DIR)/common.h $(wildcard $(ALGORITHM_DIR)/cpu/*.h) \
        $(SIMULATOR_DIR)/state.h $(SIMULATOR_DIR)/cpu_ops.h \
        $(SIMULATOR_DIR)/benchmark.h $(SIMULATOR_DIR)/vortex_runner.h
HDRS += $(SIMULATOR_DIR)/gpu_tuning.h
VX_HDRS := $(SRC_DIR)/common.h $(SIMULATOR_DIR)/state.h
KERNEL_LIB := vortex2
OPTS ?= -n4

include $(ROOT_DIR)/tests/regression/common.mk

# Match the optimization level of the standalone CPU reference.
ifndef DEBUG
CXXFLAGS += -O3 -march=native
endif
