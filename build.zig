const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const ztask = b.addLibrary(.{
        .name = "ztask",
        .linkage = .static,
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .link_libcpp = true,
        }),
    });

    // the main code
    ztask.root_module.addCSourceFiles(.{
        .language = .cpp,
        .files = &.{
            "src/z_env.cpp",
            "src/z_epoll.cpp",
            "src/z_fd.cpp",
            "src/z_net.cpp",
            "src/z_signal.cpp",
            "src/z_sleep.cpp",
            "src/z_ssl.cpp",
            "src/z_timer.cpp",
        },
        .flags = &.{
            "-std=c++23",
            "-Wall",
            "-Wextra",
            "-Werror",
            // "-Wno-unused-parameter",
            // "-Wno-unused-variable",
            // "-Wno-unused-function",
        },
    });
    ztask.installHeadersDirectory(b.path("src"), "", .{ .include_extensions = &.{ "hpp", "h" } });

    // the wolfssl dependency
    // TODO: change to lazy dependency
    const wolfssl = b.dependency("wolfssl", .{
        .target = target,
        // .optimize = optimize,
        .lto = .none,
        .single_threaded = false,
    });
    ztask.root_module.addIncludePath(wolfssl.namedLazyPath("include"));
    ztask.root_module.addObjectFile(wolfssl.namedLazyPath("libwolfssl.a"));

    b.installArtifact(ztask);
}
