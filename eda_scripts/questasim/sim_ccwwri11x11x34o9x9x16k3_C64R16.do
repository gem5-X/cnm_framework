project open PROJ_CR/HBM_CR.mpf

sccom -work work -stats=none /scrap/users/rmedina/pim_cores/MGC_QSIM/PROJ_CR/pch_main.cpp
sccom -link
vsim -L /scrap/users/rmedina/pim_cores/LIBS/TSMC28HPC_CORE_VLOG -sdftyp top/IMCcoreUnderTest_0=/scrap/users/rmedina/pim_cores/CDS_GENUS/TIM/imc_C64R16S16_1pch.sdf work.top -t ps -sdfnoerror
vcd file /home/rmedina/VCD/hbmCR/imc_C64R16S16_ccwwri11x11x34o9x9x16k3_HBM.vcd.gz
vcd add -r top/IMCcoreUnderTest_0/*
run -all