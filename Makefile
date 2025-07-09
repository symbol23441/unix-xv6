
# To compile and run with a lab solution, set the lab name in lab.mk
# (e.g., LAB=util).  Run make grade to test solution with the lab's
# grade script (e.g., grade-lab-util).

-include conf/lab.mk

K=kernel
U=user

OBJS = \
  $K/entry.o \
  $K/kalloc.o \
  $K/string.o \
  $K/main.o \
  $K/vm.o \
  $K/proc.o \
  $K/swtch.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/fs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o \
  $K/kernelvec.o \
  $K/plic.o \
  $K/virtio_disk.o

# 内核并发检查(Kernel Concurrency Sanitizer)
OBJS_KCSAN = \
  $K/start.o \
  $K/console.o \
  $K/printf.o \
  $K/uart.o \
  $K/spinlock.o

ifdef KCSAN
OBJS_KCSAN += \
	$K/kcsan.o
endif

ifeq ($(LAB),pgtbl)
OBJS += \
	$K/vmcopyin.o
endif

ifeq ($(LAB),$(filter $(LAB), pgtbl lock))
OBJS += \
	$K/stats.o\
	$K/sprintf.o
endif


ifeq ($(LAB),net)
OBJS += \
	$K/e1000.o \
	$K/net.o \
	$K/sysnet.o \
	$K/pci.o
endif




# Try to infer the correct TOOLPREFIX if not set
# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
#TOOLPREFIX = 

# 工具链前缀设置
# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# QEMU cmd
QEMU = qemu-system-riscv64

CC = $(TOOLPREFIX)gcc							# c编译器（.c ➜ .o）
AS = $(TOOLPREFIX)gas							# 汇编编译器（.S ➜ .o）
LD = $(TOOLPREFIX)ld							# 链接器（.o ➜ .elf）   elf=可执行 + 描述信息
OBJCOPY = $(TOOLPREFIX)objcopy					# elf转裸二进制(.elf ➜ .bin）
OBJDUMP = $(TOOLPREFIX)objdump					# 反汇编查看 ELF 文件内容（生成.asm）
# 文件说明  .c c源码；.S 汇编源码；.o 可重定位ELF，不可执行; .out .elf 可执行ELF(可以被 QEMU 或 bbl 加载)；.bin 纯二进制文件（需要手动加载） 
 
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -Wno-error=infinite-recursion

ifdef LAB
LABUPPER = $(shell echo $(LAB) | tr a-z A-Z)
XCFLAGS += -DSOL_$(LABUPPER) -DLAB_$(LABUPPER)
endif

CFLAGS += $(XCFLAGS)
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

ifeq ($(LAB),net)
CFLAGS += -DNET_TESTS_PORT=$(SERVERPORT)
endif

ifdef KCSAN
CFLAGS += -DKCSAN
KCSANFLAG = -fsanitize=thread
endif

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096

$K/kernel: $(OBJS) $(OBJS_KCSAN) $K/kernel.ld $U/initcode
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $K/kernel $(OBJS) $(OBJS_KCSAN)
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

$(OBJS): EXTRAFLAG := $(KCSANFLAG)

$K/%.o: $K/%.c
	$(CC) $(CFLAGS) $(EXTRAFLAG) -c -o $@ $<


$U/initcode: $U/initcode.S
	$(CC) $(CFLAGS) -march=rv64g -nostdinc -I. -Ikernel -c $U/initcode.S -o $U/initcode.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $U/initcode.out $U/initcode.o
	$(OBJCOPY) -S -O binary $U/initcode.out $U/initcode
	$(OBJDUMP) -S $U/initcode.o > $U/initcode.asm

tags: $(OBJS) _init
	etags *.S *.c

# 最小的用户代码依赖库
ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

ifeq ($(LAB),$(filter $(LAB), pgtbl lock))
ULIB += $U/statistics.o
endif

# 通配链接，生成可执行文件_开头
# 使用链接器 `ld` 根据所有依赖（`$^`）生成最终可执行文件 `$@`，其中 `$(LDFLAGS)` 是链接器选项，`-N` 表示不要为每个段分配独立页使 ELF 更紧凑，`-e main` 指定入口为 `main` 函数，`-Ttext 0` 把 `.text` 段放在地址 0，`-o $@` 指定输出文件名为目标名（如 `_cat`）。
# 导出符号表为 .sym
_%: %.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

# 系统调用接口.asm生成（使用 Perl 脚本 usys.pl 自动生成系统调用接口的汇编源文件 usys.S）
$U/usys.S : $U/usys.pl
	perl $U/usys.pl > $U/usys.S

$U/usys.o : $U/usys.S
	$(CC) $(CFLAGS) -c -o $U/usys.o $U/usys.S

# forkTest 精简编译
$U/_forktest: $U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
	$(OBJDUMP) -S $U/_forktest > $U/forktest.asm

# 文件系统构建程序 
mkfs/mkfs: mkfs/mkfs.c $K/fs.h $K/param.h
	gcc $(XCFLAGS) -Werror -Wall -I. -o mkfs/mkfs mkfs/mkfs.c

# 编译完保留.o文件
# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o



# =====================================打包文件系统（用户程序+文件）=============================
UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\

ifeq ($(LAB),$(filter $(LAB), pgtbl lock))
UPROGS += \
	$U/_stats
endif

ifeq ($(LAB),traps)
UPROGS += \
	$U/_call\
	$U/_bttest
endif

ifeq ($(LAB),lazy)
UPROGS += \
	$U/_lazytests
endif

ifeq ($(LAB),cow)
UPROGS += \
	$U/_cowtest
endif

ifeq ($(LAB),thread)
UPROGS += \
	$U/_uthread

$U/uthread_switch.o : $U/uthread_switch.S
	$(CC) $(CFLAGS) -c -o $U/uthread_switch.o $U/uthread_switch.S

$U/_uthread: $U/uthread.o $U/uthread_switch.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_uthread $U/uthread.o $U/uthread_switch.o $(ULIB)
	$(OBJDUMP) -S $U/_uthread > $U/uthread.asm

ph: notxv6/ph.c
	gcc -o ph -g -O2 $(XCFLAGS) notxv6/ph.c -pthread

barrier: notxv6/barrier.c
	gcc -o barrier -g -O2 $(XCFLAGS) notxv6/barrier.c -pthread
endif

ifeq ($(LAB),lock)
UPROGS += \
	$U/_kalloctest\
	$U/_bcachetest
endif

ifeq ($(LAB),fs)
UPROGS += \
	$U/_bigfile
endif


ifeq ($(LAB),net)
UPROGS += \
	$U/_nettests
endif

UEXTRA=
ifeq ($(LAB),util)
	UEXTRA += user/xargstest.sh
endif


fs.img: mkfs/mkfs README $(UEXTRA) $(UPROGS)
	mkfs/mkfs fs.img README $(UEXTRA) $(UPROGS)
# =====================================启动依赖自编译=============================
# （依赖改动，相关文件重新编译，.d文件由CFLAGS += -MD支持） 
-include kernel/*.d user/*.d

# =====================================make clean=============================
clean: 
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$U/initcode $U/initcode.out $K/kernel fs.img \
	mkfs/mkfs .gdbinit \
        $U/usys.S \
	$(UPROGS) \
	ph barrier

# =====================================qemu 启动设置=============================
# 生成不冲突的gdb端口（根据pid）
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# 根据QEMU版本，设置gdb命令参数
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
# 默认CPU数量
ifndef CPUS
CPUS := 3
endif
ifeq ($(LAB),fs)
CPUS := 1
endif

# UDP 端口设定
FWDPORT = $(shell expr `id -u` % 5000 + 25999)

# -machine virt     使用 QEMU 提供的 RISC-V "virt" 虚拟开发板模型
# -bios none        裸机启动方式，无需bbl or bios
# -kernel $K/kernel 指定加载并运行的 xv6 内核（已与 bbl 链接）
# -m 128M           分配 128MB 内存给虚拟机
# -smp $(CPUS)      启动多个 CPU 核心（默认为 3，根据实验可调）
# -nographic        不启用图形界面，所有输入输出都走终端（方便调试/GDB）

# 定义磁盘镜像
# -drive               添加一个虚拟硬盘或 CD-ROM 驱动设备
# file=fs.img          使用 fs.img 作为虚拟磁盘镜像（即 xv6 文件系统）
# if=none              不直接绑定控制器接口，留待 -device 手动绑定
# format=raw           磁盘格式为原始二进制镜像（非 qcow2 等虚拟格式）
# id=x0                给该磁盘设备指定 ID 名称为 x0，供后续引用

# 硬盘挂到总线的设置
# -device virtio-blk-device     添加一个 VirtIO 块设备（xv6 支持的高性能磁盘）
# drive=x0                      指定该 virtio 设备使用 id 为 x0 的磁盘镜像
# bus=virtio-mmio-bus.0         将设备挂载到 virtio 的 MMIO 总线（xv6 默认识别该总线）
QEMUOPTS = -machine virt -bios none -kernel $K/kernel -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

# 网络设置
ifeq ($(LAB),net)
QEMUOPTS += -netdev user,id=net0,hostfwd=udp::$(FWDPORT)-:2000 -object filter-dump,id=net0,netdev=net0,file=packets.pcap
QEMUOPTS += -device e1000,netdev=net0,bus=pcie.0
endif

# qemu虚拟机启动
qemu: $K/kernel fs.img
	$(QEMU) $(QEMUOPTS)

# 生成适配当前 GDB 端口的 .gdbinit 文件，用于自动连接到 QEMU 的 GDB 调试端口。
.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@
# 启动 xv6 QEMU 虚拟机，但暂停在启动前，让你用 gdb 连接调试。
qemu-gdb: $K/kernel .gdbinit fs.img
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

# 网络实验相关的模拟服务
ifeq ($(LAB),net)
# try to generate a unique port for the echo server
SERVERPORT = $(shell expr `id -u` % 5000 + 25099)

server:
	python3 server.py $(SERVERPORT)

ping:
	python3 ping.py $(FWDPORT)
endif

####=====================================评分脚本=============================
##  FOR testing lab grading script
##

ifneq ($(V),@)
GRADEFLAGS += -v
endif

print-gdbport:
	@echo $(GDBPORT)

grade:
	@echo $(MAKE) clean
	@$(MAKE) clean || \
          (echo "'make clean' failed.  HINT: Do you have another running instance of xv6?" && exit 1)
	./grade-lab-$(LAB) $(GRADEFLAGS)


##=====================================上传作业脚本=============================
## FOR web handin
## 打包与提交系统
##
WEBSUB := https://6828.scripts.mit.edu/2021/handin.py

handin: tarball-pref myapi.key
	@SUF=$(LAB); \
	curl -f -F file=@lab-$$SUF-handin.tar.gz -F key=\<myapi.key $(WEBSUB)/upload \
	    > /dev/null || { \
		echo ; \
		echo Submit seems to have failed.; \
		echo Please go to $(WEBSUB)/ and upload the tarball manually.; }

handin-check:
	@if ! test -d .git; then \
		echo No .git directory, is this a git repository?; \
		false; \
	fi
	@if test "$$(git symbolic-ref HEAD)" != refs/heads/$(LAB); then \
		git branch; \
		read -p "You are not on the $(LAB) branch.  Hand-in the current branch? [y/N] " r; \
		test "$$r" = y; \
	fi
	@if ! git diff-files --quiet || ! git diff-index --quiet --cached HEAD; then \
		git status -s; \
		echo; \
		echo "You have uncomitted changes.  Please commit or stash them."; \
		false; \
	fi
	@if test -n "`git status -s`"; then \
		git status -s; \
		read -p "Untracked files will not be handed in.  Continue? [y/N] " r; \
		test "$$r" = y; \
	fi

UPSTREAM := $(shell git remote -v | grep -m 1 "xv6-labs-2021" | awk '{split($$0,a," "); print a[1]}')

tarball: handin-check
	git archive --format=tar HEAD | gzip > lab-$(LAB)-handin.tar.gz

tarball-pref: handin-check
	@SUF=$(LAB); \
	git archive --format=tar HEAD > lab-$$SUF-handin.tar; \
	git diff $(UPSTREAM)/$(LAB) > /tmp/lab-$$SUF-diff.patch; \
	tar -rf lab-$$SUF-handin.tar /tmp/lab-$$SUF-diff.patch; \
	gzip -c lab-$$SUF-handin.tar > lab-$$SUF-handin.tar.gz; \
	rm lab-$$SUF-handin.tar; \
	rm /tmp/lab-$$SUF-diff.patch; \

myapi.key:
	@echo Get an API key for yourself by visiting $(WEBSUB)/
	@read -p "Please enter your API key: " k; \
	if test `echo "$$k" |tr -d '\n' |wc -c` = 32 ; then \
		TF=`mktemp -t tmp.XXXXXX`; \
		if test "x$$TF" != "x" ; then \
			echo "$$k" |tr -d '\n' > $$TF; \
			mv -f $$TF $@; \
		else \
			echo mktemp failed; \
			false; \
		fi; \
	else \
		echo Bad API key: $$k; \
		echo An API key should be 32 characters long.; \
		false; \
	fi;


.PHONY: handin tarball tarball-pref clean grade handin-check
