# ==========================================
# ByteOS
#
#   make        — собрать ВСЁ (Rust FS + kernel + ISO + IMG)
#   make run    — запустить (ISO в QEMU; соберёт, если ещё нет)
#   make run-img — то же для IMG
#   make clean
# ==========================================

.DEFAULT_GOAL := all

CC = gcc
LD = ld
NASM = nasm

CFLAGS = -m64 -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone \
         -mcmodel=kernel -O2 -Wall -Wextra -Wno-unused -Wno-maybe-uninitialized \
         -std=gnu11 \
         -Iinclude -Ikernel -Isrc -Ikernel/include \
         -Isrc/gui/apps/doom -Isrc/gui/apps/doom/engine \
         -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200

# fs_include ONLY for Doom (avoids breaking stdbool/mouse in kernel)
CFLAGS_DOOM = $(CFLAGS) -Isrc/gui/apps/doom/fs_include

LDFLAGS = -m elf_x86_64 -no-pie --no-warn-rwx-segments --allow-multiple-definition \
          -T kernel/linker.ld

# FAT32 filesystem logic is Rust (mandatory). Requires cargo/rustc.
RUST_FS_DIR = rust/byteos_fs
RUST_FS_LIB = $(RUST_FS_DIR)/target/release/libbyteos_fs.a
RUST_APPS_DIR = rust/byteos_apps
RUST_APPS_LIB = $(RUST_APPS_DIR)/target/release/libbyteos_apps.a
RUST_TG_DIR = rust/byteos_tg
RUST_TG_LIB = $(RUST_TG_DIR)/target/x86_64-unknown-none/release/libbyteos_tg.a
RUST_LINK = $(RUST_TG_LIB) $(RUST_FS_LIB) $(RUST_APPS_LIB)
ifeq ($(shell command -v cargo 2>/dev/null),)
  $(error Нужен cargo/rustc. Установи: sudo apt install -y cargo rustc)
endif

# ==========================================
# Doom engine (doomgeneric) — всегда в сборке
# ==========================================

DOOM_ENGINE_SRCS = \
    am_map.c d_event.c d_items.c d_iwad.c d_loop.c d_main.c d_mode.c d_net.c \
    doomdef.c doomgeneric.c doomstat.c dstrings.c dummy.c \
    f_finale.c f_wipe.c g_game.c hu_lib.c hu_stuff.c \
    i_cdmus.c i_endoom.c i_input.c i_joystick.c i_scale.c i_sound.c \
    i_system_fs.c i_timer.c i_video.c info.c \
    m_argv.c m_bbox.c m_cheat.c m_config.c m_controls.c m_fixed.c \
    m_menu.c m_misc.c m_random.c memio.c \
    p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c \
    p_map.c p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c p_setup.c \
    p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c \
    r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c \
    s_sound.c sha1.c sounds.c st_lib.c st_stuff.c statdump.c tables.c \
    v_video.c w_checksum.c w_file.c w_file_stdc.c w_main.c w_wad.c \
    wi_stuff.c z_zone.c

# ==========================================
# Объектные файлы
# ==========================================

OBJS = \
    build/kernel/kernel.o \
    build/kernel/idt.o \
    build/kernel/pic.o \
    build/kernel/resources.o \
    build/kernel/assets.o \
    build/kernel/isr_stubs.o \
    build/kernel/panic.o \
    build/kernel/timer.o \
    build/src/lib/kmem.o \
    build/src/lib/rust_glue.o \
    build/src/desktop.o \
    build/src/drivers/system/keyboard.o \
    build/src/drivers/system/mouse.o \
    build/src/drivers/system/ata.o \
    build/src/drivers/system/fat32.o \
    build/src/drivers/pci/pci.o \
    build/src/net/net.o \
    build/src/net/e1000.o \
    build/src/net/rtl8139.o \
    build/src/audio/sound.o \
    build/src/audio/hda.o \
    build/src/audio/ac97.o \
    build/src/audio/es1370.o \
    build/src/gui/gfx.o \
    build/src/gui/font/font.o \
    build/src/gui/font/font_data.o \
    build/src/gui/assets_index.o \
    build/src/gui/theme.o \
    build/src/gui/wm.o \
    build/src/gui/ui.o \
    build/src/gui/dock.o \
    build/src/gui/menubar.o \
    build/src/gui/overlays.o \
    build/src/gui/lock_screen.o \
    build/src/gui/launchpad.o \
    $(patsubst %.c,build/%.o,$(filter-out src/gui/apps/doom/%,$(wildcard src/gui/apps/*.c) $(wildcard src/gui/apps/*/*.c))) \
    $(patsubst %.c,build/%.o,$(wildcard src/gui/apps/doom/*.c)) \
    build/src/libcompat.o \
    $(addprefix build/src/gui/apps/doom/engine/,$(DOOM_ENGINE_SRCS:.c=.o))

# Generated sources (fonts, icons, wallpapers). They are committed, so
# Python is only needed when you change tools/*.py:
#   make assets
# Assets are local files in assets/gen/*.qoi (no Python on make).
# Optional regen (if you have python): make assets-regen

assets-regen:
	@echo "Опционально: python3 tools/gen_fonts.py && python3 tools/gen_assets.py && python3 tools/gen_asm.py"
	@echo "Сборка make Python не требует."

build/kernel/assets.o: kernel/assets.asm
	mkdir -p $(@D)
	$(NASM) -f elf64 $< -o $@



build/src/gui/apps/doom/%.o: src/gui/apps/doom/%.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS_DOOM) -c $< -o $@

build/src/gui/apps/doom/engine/%.o: src/gui/apps/doom/engine/%.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS_DOOM) -c $< -o $@

# ==========================================
# Rust: byteos_fs (FAT32 logic)
# ==========================================
$(RUST_FS_LIB): $(RUST_FS_DIR)/src/lib.rs $(RUST_FS_DIR)/Cargo.toml
	@echo "==> Rust: byteos_fs (FAT32)"
	cd $(RUST_FS_DIR) && RUSTFLAGS="-C code-model=kernel -C relocation-model=static -C panic=abort -C opt-level=2" cargo build --release
	@test -f $(RUST_FS_LIB)

$(RUST_APPS_LIB): $(RUST_APPS_DIR)/src/lib.rs $(RUST_APPS_DIR)/Cargo.toml
	@echo "==> Rust: byteos_apps (Browser + Telegram)"
	cd $(RUST_APPS_DIR) && RUSTFLAGS="-C code-model=kernel -C relocation-model=static -C panic=abort -C opt-level=2" cargo build --release
	@test -f $(RUST_APPS_LIB)

# Telegram (RyzikOS MTProto client) + the general heap
$(RUST_TG_LIB): $(wildcard $(RUST_TG_DIR)/src/*.rs $(RUST_TG_DIR)/src/*/*.rs) $(RUST_TG_DIR)/Cargo.toml
	@echo "==> Rust: byteos_tg (Telegram)"
	@rustc --print target-list 2>/dev/null | grep -qx x86_64-unknown-none && \
	  { ! command -v rustup >/dev/null || rustup target list --installed | grep -qx x86_64-unknown-none; } || \
	  { echo "Нужна цель x86_64-unknown-none: rustup target add x86_64-unknown-none (см. README_RU.md)"; exit 1; }
	cd $(RUST_TG_DIR) && cargo build --release
	@test -f $(RUST_TG_LIB)

.PHONY: all clean iso img run run-iso run-img doom-check assets

all: doom-check build/ByteOS.iso build/ByteOS.img
	@echo "OK: build/ByteOS.iso + build/ByteOS.img"
	@echo "==> OK: build/kernel.elf  build/ByteOS.iso  build/ByteOS.img"
	@echo "    make run"


doom-check:
	@test -f src/gui/apps/doom/DOOM.WAD \
		|| (echo "ERROR: нужен файл src/gui/apps/doom/DOOM.WAD" && exit 1)
	@test -f src/gui/apps/doom/doom.bmp \
		|| echo "WARN: нет src/gui/apps/doom/doom.bmp"
	@test -d src/gui/apps/doom/fs_include \
		|| (echo "ERROR: нет src/gui/apps/doom/fs_include/ — распакуй ByteOS_doom_fix.zip" && exit 1)
	@test -f src/gui/apps/doom/doom_fs_libc.c \
		|| (echo "ERROR: нет doom_fs_libc.c" && exit 1)
	@test -f src/gui/apps/doom/engine/i_system_fs.c \
		|| (echo "ERROR: нет engine/i_system_fs.c" && exit 1)
	@echo "OK: DOOM.WAD + doom sources + fs_include"

# ==========================================
# Компиляция kernel/include/*.c
# ==========================================

build/kernel/%.o: kernel/include/%.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# ==========================================
# Компиляция обычных C-файлов
# ==========================================


# Doom objects need fs_include
build/src/gui/apps/doom/engine/%.o: src/gui/apps/doom/engine/%.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS_DOOM) -c $< -o $@

build/src/gui/apps/doom/doom_fs_libc.o: src/gui/apps/doom/doom_fs_libc.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS_DOOM) -c $< -o $@

build/src/gui/apps/doom/doomgeneric_igoros.o: src/gui/apps/doom/doomgeneric_igoros.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS_DOOM) -c $< -o $@

build/src/gui/apps/doom/doom_app.o: src/gui/apps/doom/doom_app.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS_DOOM) -c $< -o $@

build/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# ==========================================
# ASM
# ==========================================

build/%.o: %.asm
	mkdir -p $(@D)
	$(NASM) -f elf64 $< -o $@

# ==========================================
# Ядро
# ==========================================

build/kernel.elf: $(OBJS) $(RUST_FS_LIB) $(RUST_APPS_LIB) $(RUST_TG_LIB)
	mkdir -p $(@D)
	@echo "==> Link kernel.elf"
	$(LD) $(LDFLAGS) $(OBJS) $(RUST_LINK) -o $@

# ==========================================
# ISO
# ==========================================

iso: build/ByteOS.iso

build/ByteOS.iso: build/kernel.elf boot/limine/limine.conf boot/limine/limine.cfg
	@echo "==> Создание структуры ISO..."
	rm -rf ready
	mkdir -p ready/boot/limine
	mkdir -p ready/EFI/BOOT
	mkdir -p ready/SYSTEM
	# EFI — основная загрузка
	cp build/kernel.elf ready/EFI/BOOT/kernel.elf
	cp boot/limine/limine.conf ready/EFI/BOOT/limine.conf
	cp boot/limine/limine.cfg  ready/EFI/BOOT/limine.cfg
	-cp boot/limine/BOOTX64.EFI ready/EFI/BOOT/BOOTX64.EFI 2>/dev/null || true
	# BIOS fallback
	cp build/kernel.elf ready/boot/kernel.elf
	cp boot/limine/limine.conf ready/boot/limine/limine.conf
	cp boot/limine/limine.cfg  ready/boot/limine/limine.cfg
	cp boot/limine/limine-bios-cd.bin   ready/boot/limine/
	cp boot/limine/limine-uefi-cd.bin   ready/boot/limine/
	cp boot/limine/limine-bios.sys      ready/boot/limine/
	# System marker on ISO (visible if mounted)
	printf 'ByteOS\nversion 1.0\n' > ready/SYSTEM/VERSION.TXT
	# Limine also looks for conf at root sometimes
	cp boot/limine/limine.conf ready/limine.conf
	cp boot/limine/limine.cfg  ready/limine.cfg
	xorriso -as mkisofs -R -J -joliet-long \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot \
		-boot-load-size 4 \
		-boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part \
		--efi-boot-image \
		--protective-msdos-label \
		--eltorito-alt-boot \
		ready \
		-o build/ByteOS.iso
	chmod +x ./boot/limine/limine
	./boot/limine/limine bios-install build/ByteOS.iso

# ==========================================
# IMG
# ==========================================

img: build/ByteOS.img

build/ByteOS.img: build/kernel.elf boot/limine/limine.conf boot/limine/limine.cfg boot/limine/BOOTX64.EFI boot/limine/limine-bios.sys
	@echo "==> Создание IMG (EFI + System)..."
	dd if=/dev/zero of=$@ bs=1M count=1024 status=none
	parted -s $@ mklabel msdos
	parted -s $@ mkpart primary fat32 1MiB 100%
	parted -s $@ set 1 boot on
	mformat -i $@@@1M -F ::
	# --- EFI (UEFI boot) ---
	mmd -i $@@@1M ::/EFI
	mmd -i $@@@1M ::/EFI/BOOT
	mcopy -o -i $@@@1M boot/limine/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -o -i $@@@1M build/kernel.elf ::/EFI/BOOT/kernel.elf
	mcopy -o -i $@@@1M boot/limine/limine.conf ::/EFI/BOOT/limine.conf
	mcopy -o -i $@@@1M boot/limine/limine.cfg ::/EFI/BOOT/limine.cfg
	# --- BIOS: точные имена, которые ищет Limine stage2 ---
	mmd -i $@@@1M ::/boot
	mmd -i $@@@1M ::/boot/limine
	mcopy -o -i $@@@1M boot/limine/limine-bios.sys ::/boot/limine/limine-bios.sys
	mcopy -o -i $@@@1M boot/limine/limine-bios.sys ::/limine-bios.sys
	mcopy -o -i $@@@1M build/kernel.elf ::/boot/kernel.elf
	mcopy -o -i $@@@1M boot/limine/limine.conf ::/boot/limine/limine.conf
	mcopy -o -i $@@@1M boot/limine/limine.cfg ::/boot/limine/limine.cfg
	mcopy -o -i $@@@1M boot/limine/limine.conf ::/limine.conf
	mcopy -o -i $@@@1M boot/limine/limine.cfg ::/limine.cfg
	# --- System volume (видно в File Manager) ---
	mmd -i $@@@1M ::/SYSTEM
	printf 'ByteOS\nversion 1.0\n' | mcopy -i $@@@1M - ::/SYSTEM/VERSION.TXT
	printf 'ByteOS system volume\n' | mcopy -i $@@@1M - ::/SYSTEM/README.TXT
	mmd -i $@@@1M ::/SYSTEM/APPS
	mmd -i $@@@1M ::/SYSTEM/CONFIG
	printf 'desktop=nord\n' | mcopy -i $@@@1M - ::/SYSTEM/CONFIG/DESKTOP.CFG
	chmod +x ./boot/limine/limine
	./boot/limine/limine bios-install $@
	@echo "==> IMG: $@"

# ==========================================
# QEMU
# ==========================================

run: run-iso

run-iso: build/ByteOS.iso
	qemu-system-x86_64 \
		-cdrom build/ByteOS.iso \
		-m 2048 \
		-vga std \
		-serial stdio \
		-nic user,model=e1000 \
		-audiodev pa,id=snd0 -device intel-hda -device hda-output,audiodev=snd0 \
		-no-shutdown \
		-no-reboot

run-uefi: build/ByteOS.img
	qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd \
		-drive format=raw,file=build/ByteOS.img \
		-m 2048 -vga std -serial stdio -no-shutdown -no-reboot

run-img: build/ByteOS.img
	qemu-system-x86_64 \
		-hda build/ByteOS.img \
		-m 2048 \
		-vga std \
		-serial stdio \
		-no-shutdown \
		-no-reboot

# ==========================================
# CLEAN
# ==========================================

clean:
	@if command -v cargo >/dev/null 2>&1; then cd $(RUST_FS_DIR) && cargo clean; fi
	rm -rf build ready
assets:
	@echo "OK: local resources"
