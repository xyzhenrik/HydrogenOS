CROSS ?= x86_64-elf-
GFXMODE ?= 1280x720x32
GFXW := $(word 1,$(subst x, ,$(GFXMODE)))
GFXH := $(word 2,$(subst x, ,$(GFXMODE)))
GFXD := $(word 3,$(subst x, ,$(GFXMODE)))
ARCH ?= x86_64
ARCH_DIR = kernel/arch/$(ARCH)

ifneq (, $(shell which $(CROSS)gcc 2>/dev/null))
CC = $(CROSS)gcc
LD = $(CROSS)ld
AS = $(CROSS)as
OBJCOPY = $(CROSS)objcopy
else
CC = gcc
LD = ld
AS = as
OBJCOPY = objcopy
endif

DEPFLAGS = -MMD -MP
CFLAGS_BASE = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-pic -fno-stack-protector $(DEPFLAGS) -Ikernel -Ikernel/include -Ikernel/arch/$(ARCH) -Ikernel/mm

ifeq ($(ARCH),x86_64)
CFLAGS = $(CFLAGS_BASE) -m64 -mno-red-zone -mcmodel=large
USER_CFLAGS = -m64 -mcmodel=large -ffreestanding -nostdlib -fno-pic -fno-stack-protector -O2 -Wall -Wextra $(DEPFLAGS) -Iuserspace/lib
USER_ASFLAGS = -m64 -mcmodel=large -ffreestanding -nostdlib -fno-pic -fno-stack-protector -O2 -Wall -Wextra
USER_LDFLAGS = -T userspace/hello/linker.ld -nostdlib -m elf_x86_64
KERNEL_LINKER = kernel/linker64.ld
LDFLAGS = -T $(KERNEL_LINKER) -nostdlib -m elf_x86_64
ASFLAGS = -64
KERNEL_SRC = kernel/core/kernel64.c
PAGING_SRC = kernel/mm/paging64.c
INITRD = $(BUILD_DIR)/initrd.cpio
USER_HELLO_OBJ = $(BUILD_DIR)/user_hello.o
USER_HELLO_ELF = $(BUILD_DIR)/user_hello.elf
USER_HELLO_BINOBJ = $(BUILD_DIR)/user_hello_elf.o
USER_ECHO_OBJ = $(BUILD_DIR)/user_echo.o
USER_ECHO_ELF = $(BUILD_DIR)/user_echo.elf
USER_ECHO_BINOBJ = $(BUILD_DIR)/user_echo_elf.o
USER_CAT_OBJ = $(BUILD_DIR)/user_cat.o
USER_CAT_ELF = $(BUILD_DIR)/user_cat.elf
USER_CAT_BINOBJ = $(BUILD_DIR)/user_cat_elf.o
USER_TIME_OBJ = $(BUILD_DIR)/user_time.o
USER_TIME_ELF = $(BUILD_DIR)/user_time.elf
USER_TIME_BINOBJ = $(BUILD_DIR)/user_time_elf.o
USER_SLEEP_OBJ = $(BUILD_DIR)/user_sleep.o
USER_SLEEP_ELF = $(BUILD_DIR)/user_sleep.elf
USER_SLEEP_BINOBJ = $(BUILD_DIR)/user_sleep_elf.o
USER_PIPEREAD_OBJ = $(BUILD_DIR)/user_piperead.o
USER_PIPEREAD_ELF = $(BUILD_DIR)/user_piperead.elf
USER_PIPEDELAYREAD_OBJ = $(BUILD_DIR)/user_pipedelayread.o
USER_PIPEDELAYREAD_ELF = $(BUILD_DIR)/user_pipedelayread.elf
USER_PIPEPRODUCER_OBJ = $(BUILD_DIR)/user_pipeproducer.o
USER_PIPEPRODUCER_ELF = $(BUILD_DIR)/user_pipeproducer.elf
USER_PIPECONSUMER_OBJ = $(BUILD_DIR)/user_pipeconsumer.o
USER_PIPECONSUMER_ELF = $(BUILD_DIR)/user_pipeconsumer.elf
USER_LOGPRODUCER_A_OBJ = $(BUILD_DIR)/user_logproducer_a.o
USER_LOGPRODUCER_A_ELF = $(BUILD_DIR)/user_logproducer_a.elf
USER_LOGPRODUCER_B_OBJ = $(BUILD_DIR)/user_logproducer_b.o
USER_LOGPRODUCER_B_ELF = $(BUILD_DIR)/user_logproducer_b.elf
USER_LOGCOLLECTOR_OBJ = $(BUILD_DIR)/user_logcollector.o
USER_LOGCOLLECTOR_ELF = $(BUILD_DIR)/user_logcollector.elf
USER_BOOTPROBE_OBJ = $(BUILD_DIR)/user_bootprobe.o
USER_BOOTPROBE_ELF = $(BUILD_DIR)/user_bootprobe.elf
USER_HANDOFFCHECK_OBJ = $(BUILD_DIR)/user_handoffcheck.o
USER_HANDOFFCHECK_ELF = $(BUILD_DIR)/user_handoffcheck.elf
USER_STDIOCHECK_OBJ = $(BUILD_DIR)/user_stdiocheck.o
USER_STDIOCHECK_ELF = $(BUILD_DIR)/user_stdiocheck.elf
USER_STDIOBADBIND_OBJ = $(BUILD_DIR)/user_stdiobadbind.o
USER_STDIOBADBIND_ELF = $(BUILD_DIR)/user_stdiobadbind.elf
USER_STDIONULLCHECK_OBJ = $(BUILD_DIR)/user_stdionullcheck.o
USER_STDIONULLCHECK_ELF = $(BUILD_DIR)/user_stdionullcheck.elf
USER_DUPCHECK_OBJ = $(BUILD_DIR)/user_dupcheck.o
USER_DUPCHECK_ELF = $(BUILD_DIR)/user_dupcheck.elf
USER_HANDLELIMITCHECK_OBJ = $(BUILD_DIR)/user_handlelimitcheck.o
USER_HANDLELIMITCHECK_ELF = $(BUILD_DIR)/user_handlelimitcheck.elf
USER_FILEOFFSETCHECK_OBJ = $(BUILD_DIR)/user_fileoffsetcheck.o
USER_FILEOFFSETCHECK_ELF = $(BUILD_DIR)/user_fileoffsetcheck.elf
USER_SEEKCHECK_OBJ = $(BUILD_DIR)/user_seekcheck.o
USER_SEEKCHECK_ELF = $(BUILD_DIR)/user_seekcheck.elf
USER_POLLCHECK_OBJ = $(BUILD_DIR)/user_pollcheck.o
USER_POLLCHECK_ELF = $(BUILD_DIR)/user_pollcheck.elf
USER_POLLWAITCHECK_OBJ = $(BUILD_DIR)/user_pollwaitcheck.o
USER_POLLWAITCHECK_ELF = $(BUILD_DIR)/user_pollwaitcheck.elf
USER_POLLWAKEWRITER_OBJ = $(BUILD_DIR)/user_pollwakewriter.o
USER_POLLWAKEWRITER_ELF = $(BUILD_DIR)/user_pollwakewriter.elf
USER_POLLMANYCHECK_OBJ = $(BUILD_DIR)/user_pollmanycheck.o
USER_POLLMANYCHECK_ELF = $(BUILD_DIR)/user_pollmanycheck.elf
USER_CWDCHECK_OBJ = $(BUILD_DIR)/user_cwdcheck.o
USER_CWDCHECK_ELF = $(BUILD_DIR)/user_cwdcheck.elf
USER_FSTEST_OBJ = $(BUILD_DIR)/user_fstest.o
USER_FSTEST_ELF = $(BUILD_DIR)/user_fstest.elf
USER_LOGIN_OBJ = $(BUILD_DIR)/user_login.o
USER_LOGIN_ELF = $(BUILD_DIR)/user_login.elf
USER_PKGTEST_OBJ = $(BUILD_DIR)/user_pkgtest.o
USER_PKGTEST_ELF = $(BUILD_DIR)/user_pkgtest.elf
USER_DUPFALLBACKCHECK_OBJ = $(BUILD_DIR)/user_dupfallbackcheck.o
USER_DUPFALLBACKCHECK_ELF = $(BUILD_DIR)/user_dupfallbackcheck.elf
USER_LOGSINK_WRITER_A_OBJ = $(BUILD_DIR)/user_logsink_writer_a.o
USER_LOGSINK_WRITER_A_ELF = $(BUILD_DIR)/user_logsink_writer_a.elf
USER_LOGSINK_WRITER_B_OBJ = $(BUILD_DIR)/user_logsink_writer_b.o
USER_LOGSINK_WRITER_B_ELF = $(BUILD_DIR)/user_logsink_writer_b.elf
USER_TEEWRITER_OBJ = $(BUILD_DIR)/user_teewriter.o
USER_TEEWRITER_ELF = $(BUILD_DIR)/user_teewriter.elf
USER_TEEREADER_A_OBJ = $(BUILD_DIR)/user_teereader_a.o
USER_TEEREADER_A_ELF = $(BUILD_DIR)/user_teereader_a.elf
USER_TEEREADER_B_OBJ = $(BUILD_DIR)/user_teereader_b.o
USER_TEEREADER_B_ELF = $(BUILD_DIR)/user_teereader_b.elf
INIT_CONFIG = userspace/init/init.rc
USER_COMPOSITOR_OBJ = $(BUILD_DIR)/user_compositor.o
USER_COMPOSITOR_ELF = $(BUILD_DIR)/user_compositor.elf
USER_DESKTOPDEMO_OBJ = $(BUILD_DIR)/user_desktopdemo.o
USER_DESKTOPDEMO_ELF = $(BUILD_DIR)/user_desktopdemo.elf
USER_DESKTOPTEST_OBJ = $(BUILD_DIR)/user_desktoptest.o
USER_DESKTOPTEST_ELF = $(BUILD_DIR)/user_desktoptest.elf
USER_DESKTOPTEST_WIDGETS_OBJ = $(BUILD_DIR)/user_desktoptest_widgets.o
USER_DESKTOPTEST_LAYOUT_OBJ = $(BUILD_DIR)/user_desktoptest_layout.o
USER_DESKTOPTEST_THEME_OBJ = $(BUILD_DIR)/user_desktoptest_theme.o
USER_DESKTOPTEST_TASKBAR_OBJ = $(BUILD_DIR)/user_desktoptest_taskbar.o
USER_DESKTOPTEST_LAUNCHER_OBJ = $(BUILD_DIR)/user_desktoptest_launcher.o
USER_DESKTOPTEST_FILEMANAGER_OBJ = $(BUILD_DIR)/user_desktoptest_filemanager.o
USER_INIT_OBJ = $(BUILD_DIR)/user_init.o
USER_INIT_ELF = $(BUILD_DIR)/user_init.elf
USER_SHELL_OBJ = $(BUILD_DIR)/user_shell.o
USER_SHELL_ELF = $(BUILD_DIR)/user_shell.elf
USER_CRT0_OBJ = $(BUILD_DIR)/user_crt0.o
USER_LIBC_OBJ = $(BUILD_DIR)/user_libc.o
OBJS = $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/console.o $(BUILD_DIR)/serial.o $(BUILD_DIR)/panic.o \
       $(BUILD_DIR)/paging.o $(BUILD_DIR)/time.o $(BUILD_DIR)/pmm.o $(BUILD_DIR)/kheap.o $(BUILD_DIR)/string.o \
       $(BUILD_DIR)/syscall.o $(BUILD_DIR)/process.o $(BUILD_DIR)/pipe.o \
       $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/mouse.o $(BUILD_DIR)/elf_loader.o \
       $(BUILD_DIR)/ata.o $(BUILD_DIR)/pci.o $(BUILD_DIR)/virtio.o $(BUILD_DIR)/virtio_blk.o $(BUILD_DIR)/virtio_gpu.o $(BUILD_DIR)/virtio_input.o $(BUILD_DIR)/msi.o $(BUILD_DIR)/block.o $(BUILD_DIR)/block_cache.o $(BUILD_DIR)/mbr.o \
       $(BUILD_DIR)/ext2.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/ramfs.o \
       $(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/fb_console.o $(BUILD_DIR)/font8x16.o $(BUILD_DIR)/renderer2d.o $(BUILD_DIR)/psf2.o $(BUILD_DIR)/ttf.o $(BUILD_DIR)/tty.o \
       $(BUILD_DIR)/gdt.o $(BUILD_DIR)/gdt_asm.o \
       $(BUILD_DIR)/idt.o $(BUILD_DIR)/isr.o $(BUILD_DIR)/isr_c.o $(BUILD_DIR)/isr_stubs.o \
       $(BUILD_DIR)/syscall_arch.o $(BUILD_DIR)/syscall_entry.o \
       $(BUILD_DIR)/pic.o $(BUILD_DIR)/pit.o $(BUILD_DIR)/cpu.o $(BUILD_DIR)/apic.o
OBJS += $(USER_HELLO_BINOBJ) $(USER_ECHO_BINOBJ) $(USER_CAT_BINOBJ) $(USER_TIME_BINOBJ) $(USER_SLEEP_BINOBJ)
RUN_DEPS = $(ISO)
BUILD_DEPS = $(KERNEL_BIN) $(INITRD) $(DISK_IMG)
RUN_CMD = qemu-system-x86_64 -cdrom $(ISO) -m 512M -vga std -serial stdio
ISO_DEPS = $(KERNEL_BIN) $(GRUB_CFG) $(INITRD)
else
CFLAGS = $(CFLAGS_BASE) -m32 -mno-red-zone
KERNEL_LINKER = kernel/linker.ld
LDFLAGS = -T $(KERNEL_LINKER) -nostdlib -m elf_i386
ASFLAGS = -32
KERNEL_SRC = kernel/core/kernel.c
PAGING_SRC = kernel/mm/paging.c
OBJS = $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/pmm.o \
       $(BUILD_DIR)/idt.o $(BUILD_DIR)/isr.o $(BUILD_DIR)/isr_c.o $(BUILD_DIR)/isr_stubs.o \
       $(BUILD_DIR)/console.o $(BUILD_DIR)/serial.o $(BUILD_DIR)/panic.o $(BUILD_DIR)/pic.o $(BUILD_DIR)/pit.o \
       $(BUILD_DIR)/kheap.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/mouse.o $(BUILD_DIR)/ata.o $(BUILD_DIR)/block.o \
       $(BUILD_DIR)/block_cache.o $(BUILD_DIR)/mbr.o $(BUILD_DIR)/ext2.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/ramfs.o $(BUILD_DIR)/kernel_shell.o \
       $(BUILD_DIR)/time.o $(BUILD_DIR)/gdt.o $(BUILD_DIR)/gdt_asm.o $(BUILD_DIR)/task.o $(BUILD_DIR)/task_asm.o \
       $(BUILD_DIR)/demo_tasks.o $(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/fb_console.o $(BUILD_DIR)/font8x16.o $(BUILD_DIR)/tty.o
RUN_DEPS = $(KERNEL_BIN) $(DISK_IMG)
BUILD_DEPS = $(KERNEL_BIN) $(DISK_IMG)
RUN_CMD = qemu-system-x86_64 -kernel $(KERNEL_BIN) -m 512M -vga std -serial stdio -drive format=raw,file=$(DISK_IMG)
ISO_DEPS = $(KERNEL_BIN) $(GRUB_CFG)
endif

BUILD_DIR = build/$(ARCH)
ISO_DIR = $(BUILD_DIR)/isodir
GRUB_CFG = $(ISO_DIR)/boot/grub/grub.cfg
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
ISO = $(BUILD_DIR)/hydrogen.iso
DISK_IMG = $(BUILD_DIR)/disk.img
FS_DIR = fsroot
FS_FILES = $(shell find $(FS_DIR) -type f 2>/dev/null)
USER_ELFS = $(foreach v,$(filter USER_%_ELF,$(.VARIABLES)),$($(v)))
$(DISK_IMG): $(USER_ELFS) $(INIT_CONFIG)

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean iso build run smoke

all: build
build: $(BUILD_DEPS)

$(BUILD_DIR)/boot.o: $(ARCH_DIR)/boot.S
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -DGFX_WIDTH=$(GFXW) -DGFX_HEIGHT=$(GFXH) -DGFX_DEPTH=$(GFXD) -c $< -o $@

$(BUILD_DIR)/kernel.o: $(KERNEL_SRC) kernel/core/process.h kernel/mm/paging.h kernel/mm/pmm.h kernel/mm/kheap.h kernel/fs/vfs.h kernel/fs/ramfs.h kernel/include/multiboot.h $(ARCH_DIR)/idt.h $(ARCH_DIR)/isr.h kernel/include/console.h $(ARCH_DIR)/io.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: $(ARCH_DIR)/idt.c $(ARCH_DIR)/idt.h $(ARCH_DIR)/isr.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/isr.o: $(ARCH_DIR)/isr.S
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/isr_c.o: $(ARCH_DIR)/isr.c $(ARCH_DIR)/isr.h kernel/core/process.h kernel/mm/paging.h kernel/include/console.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/isr_stubs.o: $(ARCH_DIR)/isr_stubs.S $(ARCH_DIR)/isr.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/console.o: kernel/tty/console.c kernel/include/console.h kernel/include/serial.h $(ARCH_DIR)/io.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: $(ARCH_DIR)/serial.c kernel/include/serial.h $(ARCH_DIR)/io.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pic.o: $(ARCH_DIR)/pic.c $(ARCH_DIR)/pic.h $(ARCH_DIR)/io.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pit.o: $(ARCH_DIR)/pit.c $(ARCH_DIR)/pit.h $(ARCH_DIR)/io.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apic.o: $(ARCH_DIR)/apic.c $(ARCH_DIR)/apic.h $(ARCH_DIR)/io.h $(ARCH_DIR)/msr.h kernel/mm/paging.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cpu.o: kernel/core/cpu.c kernel/include/cpu.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: $(ARCH_DIR)/keyboard.c kernel/include/keyboard.h $(ARCH_DIR)/io.h kernel/include/console.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mouse.o: $(ARCH_DIR)/mouse.c kernel/include/mouse.h $(ARCH_DIR)/io.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel_shell.o: kernel/apps/kernel_shell.c kernel/apps/kernel_shell.h kernel/include/console.h kernel/include/keyboard.h kernel/include/tty.h kernel/drivers/ata.h kernel/fs/ext2.h kernel/fs/vfs.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ata.o: kernel/drivers/ata.c kernel/drivers/ata.h $(ARCH_DIR)/io.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pci.o: kernel/drivers/pci.c kernel/drivers/pci.h $(ARCH_DIR)/io.h kernel/include/serial.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio.o: kernel/drivers/virtio.c kernel/drivers/virtio.h kernel/mm/paging.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/string.o: kernel/core/string.c kernel/include/string.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio_blk.o: kernel/drivers/virtio_blk.c kernel/drivers/virtio_blk.h kernel/drivers/virtio.h kernel/drivers/pci.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio_gpu.o: kernel/drivers/virtio_gpu.c kernel/drivers/virtio_gpu.h kernel/drivers/virtio.h kernel/drivers/pci.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio_input.o: kernel/drivers/virtio_input.c kernel/drivers/virtio_input.h kernel/drivers/virtio.h kernel/drivers/pci.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/msi.o: kernel/drivers/msi.c kernel/drivers/msi.h kernel/drivers/pci.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/block.o: kernel/drivers/block.c kernel/drivers/block.h kernel/drivers/ata.h kernel/drivers/virtio_blk.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/block_cache.o: kernel/drivers/block_cache.c kernel/drivers/block.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mbr.o: kernel/drivers/mbr.c kernel/drivers/mbr.h kernel/drivers/block.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ext2.o: kernel/fs/ext2.c kernel/fs/ext2.h kernel/fs/vfs.h kernel/drivers/block.h kernel/drivers/mbr.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: kernel/fs/vfs.c kernel/fs/vfs.h kernel/fs/ext2.h kernel/fs/ramfs.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ramfs.o: kernel/fs/ramfs.c kernel/fs/ramfs.h kernel/fs/vfs.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/time.o: kernel/core/time.c kernel/core/time.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: $(ARCH_DIR)/gdt.c $(ARCH_DIR)/gdt.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt_asm.o: $(ARCH_DIR)/gdt.S
	mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/syscall_arch.o: $(ARCH_DIR)/syscall_arch.c $(ARCH_DIR)/syscall_arch.h $(ARCH_DIR)/msr.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall_entry.o: $(ARCH_DIR)/syscall_entry.S
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/task.o: kernel/core/task.c kernel/core/task.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/task_asm.o: $(ARCH_DIR)/task.S
	mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/demo_tasks.o: kernel/core/demo_tasks.c kernel/core/demo_tasks.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/elf_loader.o: kernel/core/elf_loader.c kernel/include/elf_loader.h kernel/include/elf64.h kernel/include/console.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/panic.o: kernel/core/panic.c kernel/include/panic.h kernel/include/console.h kernel/include/serial.h $(ARCH_DIR)/io.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(ARCH),x86_64)
$(USER_HELLO_OBJ): userspace/hello/hello.S
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_HELLO_ELF): $(USER_HELLO_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_HELLO_OBJ)

$(USER_HELLO_BINOBJ): $(USER_HELLO_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_ECHO_OBJ): userspace/echo/echo.c userspace/lib/syscall.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_ECHO_ELF): $(USER_ECHO_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_ECHO_OBJ)

$(USER_ECHO_BINOBJ): $(USER_ECHO_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_CAT_OBJ): userspace/cat/cat.c userspace/lib/syscall.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_CAT_ELF): $(USER_CAT_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_CAT_OBJ)

$(USER_CAT_BINOBJ): $(USER_CAT_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_TIME_OBJ): userspace/time/time.c userspace/lib/syscall.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_TIME_ELF): $(USER_TIME_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_TIME_OBJ)

$(USER_TIME_BINOBJ): $(USER_TIME_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_SLEEP_OBJ): userspace/sleep/sleep.c userspace/lib/syscall.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_SLEEP_ELF): $(USER_SLEEP_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_SLEEP_OBJ)

$(USER_SLEEP_BINOBJ): $(USER_SLEEP_ELF)
	$(LD) -r -b binary -o $@ $<

$(USER_PIPEREAD_OBJ): userspace/piperead/piperead.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_PIPEREAD_ELF): $(USER_PIPEREAD_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_PIPEREAD_OBJ)

$(USER_PIPEDELAYREAD_OBJ): userspace/pipedelayread/pipedelayread.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_PIPEDELAYREAD_ELF): $(USER_PIPEDELAYREAD_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_PIPEDELAYREAD_OBJ)

$(USER_BOOTPROBE_OBJ): userspace/bootprobe/bootprobe.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_BOOTPROBE_ELF): $(USER_BOOTPROBE_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_BOOTPROBE_OBJ)

$(USER_HANDOFFCHECK_OBJ): userspace/handoffcheck/handoffcheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_HANDOFFCHECK_ELF): $(USER_HANDOFFCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_HANDOFFCHECK_OBJ)

$(USER_STDIOCHECK_OBJ): userspace/stdiocheck/stdiocheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_STDIOCHECK_ELF): $(USER_STDIOCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_STDIOCHECK_OBJ)

$(USER_STDIOBADBIND_OBJ): userspace/stdiobadbind/stdiobadbind.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_STDIOBADBIND_ELF): $(USER_STDIOBADBIND_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_STDIOBADBIND_OBJ)

$(USER_STDIONULLCHECK_OBJ): userspace/stdionullcheck/stdionullcheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_STDIONULLCHECK_ELF): $(USER_STDIONULLCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_STDIONULLCHECK_OBJ)

$(USER_DUPCHECK_OBJ): userspace/dupcheck/dupcheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DUPCHECK_ELF): $(USER_DUPCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_DUPCHECK_OBJ)

$(USER_HANDLELIMITCHECK_OBJ): userspace/handlelimitcheck/handlelimitcheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_HANDLELIMITCHECK_ELF): $(USER_HANDLELIMITCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_HANDLELIMITCHECK_OBJ)

$(USER_FILEOFFSETCHECK_OBJ): userspace/fileoffsetcheck/fileoffsetcheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_FILEOFFSETCHECK_ELF): $(USER_FILEOFFSETCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_FILEOFFSETCHECK_OBJ)

$(USER_SEEKCHECK_OBJ): userspace/seekcheck/seekcheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_SEEKCHECK_ELF): $(USER_SEEKCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_SEEKCHECK_OBJ)

$(USER_POLLCHECK_OBJ): userspace/pollcheck/pollcheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_POLLCHECK_ELF): $(USER_POLLCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_POLLCHECK_OBJ)

$(USER_POLLWAITCHECK_OBJ): userspace/pollwaitcheck/pollwaitcheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_POLLWAITCHECK_ELF): $(USER_POLLWAITCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_POLLWAITCHECK_OBJ)

$(USER_POLLWAKEWRITER_OBJ): userspace/pollwakewriter/pollwakewriter.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_POLLWAKEWRITER_ELF): $(USER_POLLWAKEWRITER_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_POLLWAKEWRITER_OBJ)

$(USER_POLLMANYCHECK_OBJ): userspace/pollmanycheck/pollmanycheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_POLLMANYCHECK_ELF): $(USER_POLLMANYCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_POLLMANYCHECK_OBJ)

$(USER_CWDCHECK_OBJ): userspace/cwdcheck/cwdcheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_CWDCHECK_ELF): $(USER_CWDCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_CWDCHECK_OBJ)

$(USER_FSTEST_OBJ): userspace/fstest/fstest.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_FSTEST_ELF): $(USER_FSTEST_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_FSTEST_OBJ)

$(USER_LOGIN_OBJ): userspace/login/login.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_LOGIN_ELF): $(USER_LOGIN_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_LOGIN_OBJ)

$(USER_PKGTEST_OBJ): userspace/pkgtest/pkgtest.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_PKGTEST_ELF): $(USER_PKGTEST_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_PKGTEST_OBJ)

$(USER_DUPFALLBACKCHECK_OBJ): userspace/dupfallbackcheck/dupfallbackcheck.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DUPFALLBACKCHECK_ELF): $(USER_DUPFALLBACKCHECK_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_DUPFALLBACKCHECK_OBJ)

$(USER_LOGSINK_WRITER_A_OBJ): userspace/logsink_writer_a/logsink_writer_a.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_LOGSINK_WRITER_A_ELF): $(USER_LOGSINK_WRITER_A_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_LOGSINK_WRITER_A_OBJ)

$(USER_LOGSINK_WRITER_B_OBJ): userspace/logsink_writer_b/logsink_writer_b.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_LOGSINK_WRITER_B_ELF): $(USER_LOGSINK_WRITER_B_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_LOGSINK_WRITER_B_OBJ)

$(USER_TEEWRITER_OBJ): userspace/teewriter/teewriter.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_TEEWRITER_ELF): $(USER_TEEWRITER_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_TEEWRITER_OBJ)

$(USER_TEEREADER_A_OBJ): userspace/teereader_a/teereader_a.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_TEEREADER_A_ELF): $(USER_TEEREADER_A_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_TEEREADER_A_OBJ)

$(USER_TEEREADER_B_OBJ): userspace/teereader_b/teereader_b.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_TEEREADER_B_ELF): $(USER_TEEREADER_B_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_TEEREADER_B_OBJ)

$(USER_PIPEPRODUCER_OBJ): userspace/pipeproducer/pipeproducer.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_PIPEPRODUCER_ELF): $(USER_PIPEPRODUCER_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_PIPEPRODUCER_OBJ)

$(USER_PIPECONSUMER_OBJ): userspace/pipeconsumer/pipeconsumer.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_PIPECONSUMER_ELF): $(USER_PIPECONSUMER_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_PIPECONSUMER_OBJ)

$(USER_LOGPRODUCER_A_OBJ): userspace/logproducer_a/logproducer_a.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_LOGPRODUCER_A_ELF): $(USER_LOGPRODUCER_A_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_LOGPRODUCER_A_OBJ)

$(USER_LOGPRODUCER_B_OBJ): userspace/logproducer_b/logproducer_b.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_LOGPRODUCER_B_ELF): $(USER_LOGPRODUCER_B_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_LOGPRODUCER_B_OBJ)

$(USER_LOGCOLLECTOR_OBJ): userspace/logcollector/logcollector.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_LOGCOLLECTOR_ELF): $(USER_LOGCOLLECTOR_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_LOGCOLLECTOR_OBJ)

$(USER_INIT_OBJ): userspace/init/init.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_INIT_ELF): $(USER_INIT_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_INIT_OBJ)

$(USER_SHELL_OBJ): userspace/shell/shell.c userspace/lib/syscall.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_SHELL_ELF): $(USER_SHELL_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_SHELL_OBJ)

$(USER_COMPOSITOR_OBJ): userspace/compositor/compositor.c userspace/compositor/compositor.h userspace/lib/mini_libc.h userspace/lib/syscall.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuserspace/compositor -c $< -o $@

$(BUILD_DIR)/input_router.o: userspace/compositor/input_router.c userspace/compositor/input_router.h userspace/compositor/compositor.h userspace/lib/mini_libc.h userspace/lib/syscall.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuserspace/compositor -c $< -o $@

$(USER_COMPOSITOR_ELF): $(USER_COMPOSITOR_OBJ) $(BUILD_DIR)/input_router.o $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) --defsym main=comp_event_loop -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_COMPOSITOR_OBJ) $(BUILD_DIR)/input_router.o

$(USER_DESKTOPDEMO_OBJ): userspace/desktopdemo/desktopdemo.c userspace/compositor/compositor.h userspace/lib/mini_libc.h userspace/lib/syscall.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuserspace/compositor -c $< -o $@

$(USER_DESKTOPDEMO_ELF): $(USER_DESKTOPDEMO_OBJ) $(USER_COMPOSITOR_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_DESKTOPDEMO_OBJ) $(USER_COMPOSITOR_OBJ)

$(USER_DESKTOPTEST_OBJ): userspace/desktoptest/desktoptest.c userspace/compositor/compositor.h userspace/lib/mini_libc.h userspace/lib/widgets.h userspace/lib/layout.h userspace/lib/theme.h userspace/lib/taskbar.h userspace/lib/launcher.h userspace/lib/filemanager.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuserspace/compositor -c $< -o $@

$(USER_DESKTOPTEST_WIDGETS_OBJ): userspace/lib/widgets.c userspace/lib/widgets.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DESKTOPTEST_LAYOUT_OBJ): userspace/lib/layout.c userspace/lib/layout.h userspace/lib/widgets.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DESKTOPTEST_THEME_OBJ): userspace/lib/theme.c userspace/lib/theme.h userspace/lib/widgets.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DESKTOPTEST_TASKBAR_OBJ): userspace/lib/taskbar.c userspace/lib/taskbar.h userspace/lib/widgets.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DESKTOPTEST_LAUNCHER_OBJ): userspace/lib/launcher.c userspace/lib/launcher.h userspace/lib/widgets.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DESKTOPTEST_FILEMANAGER_OBJ): userspace/lib/filemanager.c userspace/lib/filemanager.h userspace/lib/widgets.h userspace/lib/mini_libc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DESKTOPTEST_ELF): $(USER_DESKTOPTEST_OBJ) $(USER_COMPOSITOR_OBJ) $(USER_DESKTOPTEST_WIDGETS_OBJ) $(USER_DESKTOPTEST_LAYOUT_OBJ) $(USER_DESKTOPTEST_THEME_OBJ) $(USER_DESKTOPTEST_TASKBAR_OBJ) $(USER_DESKTOPTEST_LAUNCHER_OBJ) $(USER_DESKTOPTEST_FILEMANAGER_OBJ) $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) userspace/hello/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_DESKTOPTEST_OBJ) $(USER_COMPOSITOR_OBJ) $(USER_DESKTOPTEST_WIDGETS_OBJ) $(USER_DESKTOPTEST_LAYOUT_OBJ) $(USER_DESKTOPTEST_THEME_OBJ) $(USER_DESKTOPTEST_TASKBAR_OBJ) $(USER_DESKTOPTEST_LAUNCHER_OBJ) $(USER_DESKTOPTEST_FILEMANAGER_OBJ)

$(USER_CRT0_OBJ): userspace/lib/crt0.S
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_LIBC_OBJ): userspace/lib/mini_libc.c userspace/lib/mini_libc.h userspace/lib/syscall.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(INITRD): $(USER_HELLO_ELF) $(USER_ECHO_ELF) $(USER_CAT_ELF) $(USER_TIME_ELF) $(USER_SLEEP_ELF) $(USER_PIPEREAD_ELF) $(USER_PIPEDELAYREAD_ELF) $(USER_PIPEPRODUCER_ELF) $(USER_PIPECONSUMER_ELF) $(USER_LOGPRODUCER_A_ELF) $(USER_LOGPRODUCER_B_ELF) $(USER_LOGCOLLECTOR_ELF) $(USER_BOOTPROBE_ELF) $(USER_HANDOFFCHECK_ELF) $(USER_STDIOCHECK_ELF) $(USER_STDIOBADBIND_ELF) $(USER_STDIONULLCHECK_ELF) $(USER_DUPCHECK_ELF) $(USER_HANDLELIMITCHECK_ELF) $(USER_FILEOFFSETCHECK_ELF) $(USER_SEEKCHECK_ELF) $(USER_POLLCHECK_ELF) $(USER_POLLWAITCHECK_ELF) $(USER_POLLWAKEWRITER_ELF) $(USER_POLLMANYCHECK_ELF) $(USER_CWDCHECK_ELF) $(USER_DUPFALLBACKCHECK_ELF) $(USER_FSTEST_ELF) $(USER_LOGIN_ELF) $(USER_PKGTEST_ELF) $(USER_LOGSINK_WRITER_A_ELF) $(USER_LOGSINK_WRITER_B_ELF) $(USER_TEEWRITER_ELF) $(USER_TEEREADER_A_ELF) $(USER_TEEREADER_B_ELF) $(USER_COMPOSITOR_ELF) $(USER_DESKTOPDEMO_ELF) $(USER_DESKTOPTEST_ELF) $(USER_INIT_ELF) $(USER_SHELL_ELF) $(INIT_CONFIG) tools/mkinitrd.py
	mkdir -p $(BUILD_DIR)
	python3 tools/mkinitrd.py $@ \
		user=$(USER_HELLO_ELF) \
		echo=$(USER_ECHO_ELF) \
		cat=$(USER_CAT_ELF) \
		time=$(USER_TIME_ELF) \
		sleep=$(USER_SLEEP_ELF) \
		piperead=$(USER_PIPEREAD_ELF) \
		pipedelayread=$(USER_PIPEDELAYREAD_ELF) \
		pipeproducer=$(USER_PIPEPRODUCER_ELF) \
		pipeconsumer=$(USER_PIPECONSUMER_ELF) \
		logproducer_a=$(USER_LOGPRODUCER_A_ELF) \
		logproducer_b=$(USER_LOGPRODUCER_B_ELF) \
		logcollector=$(USER_LOGCOLLECTOR_ELF) \
		bootprobe=$(USER_BOOTPROBE_ELF) \
		handoffcheck=$(USER_HANDOFFCHECK_ELF) \
		stdiocheck=$(USER_STDIOCHECK_ELF) \
		stdiobadbind=$(USER_STDIOBADBIND_ELF) \
		stdionullcheck=$(USER_STDIONULLCHECK_ELF) \
		dupcheck=$(USER_DUPCHECK_ELF) \
		handlelimitcheck=$(USER_HANDLELIMITCHECK_ELF) \
		fileoffsetcheck=$(USER_FILEOFFSETCHECK_ELF) \
		seekcheck=$(USER_SEEKCHECK_ELF) \
		pollcheck=$(USER_POLLCHECK_ELF) \
		pollwaitcheck=$(USER_POLLWAITCHECK_ELF) \
		pollwakewriter=$(USER_POLLWAKEWRITER_ELF) \
		pollmanycheck=$(USER_POLLMANYCHECK_ELF) \
		cwdcheck=$(USER_CWDCHECK_ELF) \
		fstest=$(USER_FSTEST_ELF) \
		login=$(USER_LOGIN_ELF) \
		pkgtest=$(USER_PKGTEST_ELF) \
		dupfallbackcheck=$(USER_DUPFALLBACKCHECK_ELF) \
		logsink_writer_a=$(USER_LOGSINK_WRITER_A_ELF) \
		logsink_writer_b=$(USER_LOGSINK_WRITER_B_ELF) \
		teewriter=$(USER_TEEWRITER_ELF) \
		teereader_a=$(USER_TEEREADER_A_ELF) \
		teereader_b=$(USER_TEEREADER_B_ELF) \
		compositor=$(USER_COMPOSITOR_ELF) \
		desktopdemo=$(USER_DESKTOPDEMO_ELF) \
		desktoptest=$(USER_DESKTOPTEST_ELF) \
		init.rc=$(INIT_CONFIG) \
		init=$(USER_INIT_ELF) \
		shell=$(USER_SHELL_ELF)
endif

$(BUILD_DIR)/syscall.o: kernel/core/syscall.c kernel/core/process.h kernel/fs/vfs.h kernel/mm/paging.h kernel/include/syscall.h $(ARCH_DIR)/isr.h kernel/include/console.h kernel/include/tty.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process.o: kernel/core/process.c kernel/core/process.h kernel/core/pipe.h kernel/fs/vfs.h kernel/mm/paging.h kernel/mm/kheap.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pipe.o: kernel/core/pipe.c kernel/core/pipe.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/framebuffer.o: kernel/video/framebuffer.c kernel/include/framebuffer.h kernel/include/multiboot.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fb_console.o: kernel/video/fb_console.c kernel/video/fb_console.h kernel/video/framebuffer.c kernel/include/framebuffer.h kernel/video/font8x16.h kernel/include/mouse.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/font8x16.o: kernel/video/font8x16.c kernel/video/font8x16.h kernel/video/font8x16_data.inc
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/renderer2d.o: kernel/video/renderer2d.c kernel/video/renderer2d.h kernel/include/framebuffer.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/psf2.o: kernel/video/psf2.c kernel/video/psf2.h kernel/video/font8x16.h kernel/include/framebuffer.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ttf.o: kernel/video/ttf.c kernel/video/ttf.h kernel/video/stb_truetype.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tty.o: kernel/tty/tty.c kernel/include/tty.h kernel/include/framebuffer.h kernel/video/fb_console.h kernel/include/console.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kheap.o: kernel/mm/kheap.c kernel/mm/kheap.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: $(PAGING_SRC) kernel/mm/paging.h kernel/mm/pmm.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pmm.o: kernel/mm/pmm.c kernel/mm/pmm.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_BIN): $(OBJS) $(KERNEL_LINKER)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(GRUB_CFG):
	mkdir -p $(ISO_DIR)/boot/grub
	echo "set timeout=1" > $(GRUB_CFG)
	echo "set default=0" >> $(GRUB_CFG)
	echo "set gfxmode=$(GFXMODE)" >> $(GRUB_CFG)
	echo "set gfxpayload=$(GFXMODE)" >> $(GRUB_CFG)
	echo "menuentry 'HydrogenOS' {" >> $(GRUB_CFG)
	echo "  multiboot /boot/kernel.bin" >> $(GRUB_CFG)
ifeq ($(ARCH),x86_64)
	echo "  module /boot/initrd.cpio" >> $(GRUB_CFG)
endif
	echo "  boot" >> $(GRUB_CFG)
	echo "}" >> $(GRUB_CFG)

iso: $(ISO)

$(ISO): $(ISO_DEPS)
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
ifeq ($(ARCH),x86_64)
	cp $(INITRD) $(ISO_DIR)/boot/initrd.cpio
endif
	grub-mkrescue -o $(ISO) $(ISO_DIR)

run: $(RUN_DEPS)
	$(RUN_CMD)

smoke:
	tools/boot_smoke.sh

clean:
	rm -rf $(BUILD_DIR)

$(DISK_IMG): $(FS_FILES)
	mkdir -p $(BUILD_DIR)
	@if command -v mke2fs >/dev/null 2>&1; then \
		rm -rf $(BUILD_DIR)/stagedisk; mkdir -p $(BUILD_DIR)/stagedisk; \
		if [ -d $(FS_DIR) ]; then cp -a $(FS_DIR)/. $(BUILD_DIR)/stagedisk/ 2>/dev/null || true; fi; \
		for f in $(BUILD_DIR)/user_*.elf; do \
			[ -e "$$f" ] || continue; \
			n=$$(basename $$f .elf); n=$${n#user_}; \
			cp $$f $(BUILD_DIR)/stagedisk/$$n; \
		done; \
		cp $(INIT_CONFIG) $(BUILD_DIR)/stagedisk/init.rc; \
		rm -f $(DISK_IMG); \
		mke2fs -t ext2 -F -b 1024 -d $(BUILD_DIR)/stagedisk $(DISK_IMG) 16M; \
	else \
		dd if=/dev/zero of=$(DISK_IMG) bs=1M count=16; \
	fi
