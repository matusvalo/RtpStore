ifeq ($(MAKECMDGOALS),debug)
	CFLAGS += -O0 -g3
else
	CFLAGS += -O3 -fno-strict-aliasing
endif
#note: -fno-strict-aliasing optimilization turned off because of warnings in rtp_network.c:read_from_sock()
# and rtp_network.c:rtp_packet_filter().
all: RtpStore

debug: RtpStore

RtpStore: $(C_OBJ) $(LIBS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	$(CC) $(LDFLAGS) -o"$(bin)/$(libname)" $(C_OBJ) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

$(bin)/%.o: $(srcdir)/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	$(CC) -c $(CFLAGS) -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '
