{ pkgs }: {
	deps = [
  pkgs.busybox
  pkgs.clang_12
		pkgs.ccls
		pkgs.gdb
		pkgs.gnumake
	];
}