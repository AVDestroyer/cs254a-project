ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := quantum-gate

ifndef QUANTUM_SRC_DIR
$(error QUANTUM_SRC_DIR must point to this project's GPU-simulations directory)
endif

SRC_DIR := $(abspath $(QUANTUM_SRC_DIR))
SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp
HDRS := $(SRC_DIR)/common.h
VX_HDRS := $(SRC_DIR)/common.h
KERNEL_LIB := vortex2
OPTS ?= -n3

include $(ROOT_DIR)/tests/regression/common.mk

