CW_VSPA_ROOT ?= C:/Freescale/CW_VSPA_v10.3.10
DFE_REF_ROOT ?= C:/Users/dheer/claude/vspa2_lib/sw1024207-DFE Ref v5.1.5(0.7)_original
FECU_REF_ROOT ?= C:/Users/dheer/claude/vspa2_lib/LA9310_FECU_orig_5/LA9310_FECU

VSPA_TOOLS := $(CW_VSPA_ROOT)/VSPA/VSPA_Tools
VSPA_CC := $(VSPA_TOOLS)/bin/fsvspacc
VSPA_ENV := $(VSPA_TOOLS)
VSPA_MAKE := $(CW_VSPA_ROOT)/gnu/bin/make.exe

BUILD_DIR := build
TARGET := $(BUILD_DIR)/apm.eld

COMMON_FLAGS := -arch vspa2 -au_count 16 -core_type sp -ansi off -g -O0 \
	-D__LA9310__ -DLA9310 \
	-opt=noalias_by_type -mvcpu -msgstyle gcc \
	-I"src" \
	-I"$(DFE_REF_ROOT)/lib" \
	-I"$(DFE_REF_ROOT)/vspa-sdk/inc" \
	-I"$(FECU_REF_ROOT)/Sources" \
	-env "$(VSPA_ENV)"

OBJS := \
	$(BUILD_DIR)/startup.eln \
	$(BUILD_DIR)/boot.eln \
	$(BUILD_DIR)/l1c_vspa_fft_service.eln

DFE_LIB := $(DFE_REF_ROOT)/lib/lib_DFE_Ref_vspa2.elb
LCF := $(DFE_REF_ROOT)/LCF/LA9310_sp.lcf

.PHONY: all clean

all: $(TARGET)

$(BUILD_DIR):
	mkdir "$(BUILD_DIR)"

$(BUILD_DIR)/startup.eln: src/startup.s | $(BUILD_DIR)
	"$(VSPA_CC)" $(COMMON_FLAGS) -o "$@" -c "$<" -o "$(BUILD_DIR)"

$(BUILD_DIR)/boot.eln: | $(BUILD_DIR)
	"$(VSPA_CC)" $(COMMON_FLAGS) -o "$@" -c "$(DFE_REF_ROOT)/vspa-sdk/src/boot.c" -o "$(BUILD_DIR)"

$(BUILD_DIR)/l1c_vspa_fft_service.eln: src/l1c_vspa_fft_service.c src/l1c_vspa_fft_service.h | $(BUILD_DIR)
	"$(VSPA_CC)" $(COMMON_FLAGS) -o "$@" -c "$<" -o "$(BUILD_DIR)"

$(TARGET): $(OBJS)
	"$(VSPA_CC)" $(COMMON_FLAGS) -mem "$(LCF)" $(OBJS) "$(DFE_LIB)" -o "$@"

clean:
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
