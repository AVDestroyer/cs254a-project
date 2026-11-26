ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := grover-algorithm

ifndef GROVER_SRC_DIR
$(error GROVER_SRC_DIR must point to the grover-algorithm/gpu directory)
endif

SRC_DIR := $(abspath $(GROVER_SRC_DIR))
SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp
HDRS := $(SRC_DIR)/common.h $(SRC_DIR)/../cpu/reference.h
VX_HDRS := $(SRC_DIR)/common.h
KERNEL_LIB := vortex2
OPTS ?=

include $(ROOT_DIR)/tests/regression/common.mk
