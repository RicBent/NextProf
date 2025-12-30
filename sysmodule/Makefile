#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		    :=	nextprof_sys
BUILD		    :=	build
SOURCES		    :=	source ../common/source
DATA		    :=	data
INCLUDES	    :=	include ../common/include
OUTPUT      	:= output
APP_TITLE       :=  NextProfiler
APP_DESCRIPTION :=  NextProfiler System Module
APP_AUTHOR      :=  Bent
APP_PRODUCT_CODE:=  CTR-N-NXPR
APP_UNIQUE_ID   :=  0x91A8C

APP_TITLE       :=  $(shell echo "$(APP_TITLE)" | cut -c1-128)
APP_DESCRIPTION :=  $(shell echo "$(APP_DESCRIPTION)" | cut -c1-256)
APP_AUTHOR      :=  $(shell echo "$(APP_AUTHOR)" | cut -c1-128)
APP_PRODUCT_CODE:=  $(shell echo $(APP_PRODUCT_CODE) | cut -c1-16)
APP_UNIQUE_ID   :=  $(shell echo $(APP_UNIQUE_ID) | cut -c1-8)

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard

CFLAGS	:=	-g -Wall -Wno-format -O3 -mword-relocations \
			-fomit-frame-pointer -ffast-math \
			$(ARCH) -std=gnu11 \
			-ffile-prefix-map=$(FILE_PREFIX)=

CFLAGS	+=	$(INCLUDE) -DARM11 -D__3DS__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export TOPDIR		:=	$(CURDIR)
export OUTPUT_DIR	:=	$(TOPDIR)/$(OUTPUT)
export OUTPUT_FILE	:=	$(OUTPUT_DIR)/$(TARGET)
export FILE_PREFIX	:=	$(abspath $(TOPDIR)/..)/

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
			$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
			$(foreach dir,$(INCLUDES),-L$(CURDIR)/$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@mkdir -p $(OUTPUT_DIR)
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -rf $(BUILD) $(OUTPUT)


#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
ifeq ($(strip $(NO_SMDH)),)
.PHONY: all
all	:	$(OUTPUT_FILE).code.bin
endif

$(OUTPUT_FILE).elf	:	$(OFILES)

$(OUTPUT_FILE)-stripped.elf: $(OUTPUT_FILE).elf
	@cp $(OUTPUT_FILE).elf $(OUTPUT_FILE)-stripped.elf
	@$(PREFIX)strip $(OUTPUT_FILE)-stripped.elf

$(OUTPUT_FILE).cia: $(OUTPUT_FILE)-stripped.elf
	@makerom -f cia -o $(OUTPUT_FILE).cia -rsf $(TOPDIR)/resources/nextprof.rsf -target t -exefslogo -elf $(OUTPUT_FILE)-stripped.elf -DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)"
	@echo "built ... $(notdir $@)"

$(OUTPUT_FILE).code.bin: $(OUTPUT_FILE).cia
	@ctrtool --exheader=$(OUTPUT_FILE).exheader.bin --exefsdir=$(OUTPUT_FILE).exefs $(OUTPUT_FILE).cia 1>/dev/null
	@mv $(OUTPUT_FILE).exefs/code.bin $(OUTPUT_FILE).code.bin
	@rm -r $(OUTPUT_FILE).exefs

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
