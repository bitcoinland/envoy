licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

exports_files([
    "gen_sh_test_runner.sh",
    "sh_test_wrapper.sh",
])

config_setting(
    name = "opt_build",
    values = {"compilation_mode": "opt"},
)

config_setting(
    name = "fastbuild_build",
    values = {"compilation_mode": "fastbuild"},
)

config_setting(
    name = "dbg_build",
    values = {"compilation_mode": "dbg"},
)

config_setting(
    name = "disable_tcmalloc",
    values = {"define": "tcmalloc=disabled"},
)

config_setting(
    name = "disable_signal_trace",
    values = {"define": "signal_trace=disabled"},
)

config_setting(
    name = "disable_hot_restart",
    values = {"define": "hot_restart=disabled"},
)
