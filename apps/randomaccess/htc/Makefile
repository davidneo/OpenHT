export CNY_PDK          ?= /work/pdk_releases
export CNY_PDK_REV      ?= latest
export CNY_PDK_PLATFORM ?= hc-2ex

export CNY_HT_AEUCNT = 16

export LDFLAGS += -fopenmp

# DO NOT MODIFY ITEMS BELOW THIS LINE
export HT_DIR ?= $(CNY_PDK)/ht
include $(HT_DIR)/ht_lib/MakefileInclude.cnyht

#
# Weave in HTC
#
HTC_DIR ?= /work/htc_releases/latest
HTC ?= $(HTC_DIR)/bin/htc

HTC_SRC = $(notdir $(wildcard src_htc/*))
HTC_OPT = -I ../src -rose:openmp:lowering -rose:skipfinalCompileStep

report sysc vsim app pers vfiles libsysc libapp: src_pers/design.htd
src_pers/design.htd: src_htc/$(HTC_SRC)
	@cd src_htc > /dev/null && \
	    $(HTC) $(HTC_OPT) -htc:split $(HTC_SRC) -htc:sysc && \
	    $(HTC) $(HTC_OPT) -DCNY_HTC_COPROC rose_$(HTC_SRC) -htc:sysc
	@mkdir -p src_pers
	@grep -v "___htc_dummy_decl" src_htc/rose_rose_$(HTC_SRC) \
	    > src_pers/rose_rose_$(HTC_SRC) && rm src_htc/rose_rose_$(HTC_SRC)
	@mv src_htc/Pers* src_pers
	@mv src_htc/*.htd src_pers
	@echo "#define CNY_HTC_HOST 1" >> src/$(basename rose_$(HTC_SRC)).cpp
	@echo "#define HIF_CODE 1" >> src/$(basename rose_$(HTC_SRC)).cpp
	@sed 's:/\* extern "C" \*/:extern "C":' < src_htc/rose_$(HTC_SRC) \
	    >> src/$(basename rose_$(HTC_SRC)).cpp
	@rm -f src_htc/rose_$(HTC_SRC)

clean distclean: cleanhtc
cleanhtc:
	@rm -rf src_pers src/rose_*
