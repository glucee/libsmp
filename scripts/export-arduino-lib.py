#!/usr/bin/env python3
import os
from os.path import isdir, isfile, join, relpath, normpath, dirname, basename
from os.path import realpath
import sys
import zipfile
import tempfile
import argparse
import re

OUTPUT_NAME = "libsmp.zip"  # default name
INCLUDED_DIRS = ["docs"]  # directories copied as-is with their tree

# directories from which we copy all the files and put them at the root
EXTRACTED_DIRS = ["include", "src"]

# files in the root that we include
INCLUDED_FILES = ["COPYING", "README.md", "library.properties"]

# files to be excluded globally
EXCLUDED_FILES = [
    ".gitignore",
    "libsmp-private-posix.h",
    "serial-device-avr.c",
    "serial-device-posix.c",
    "serial-device-win32.c",
    "libsmp-static.h.in"
]

CONFIGURATION_PARAMETERS = {
    "config.h": [],
    "libsmp-config.h": [
        ("SMP_MESSAGE_MAX_VALUES", 8,
            "The maximum number of arguments in a message"),
    ]
}

LIBSMP_STATIC_FILENAME = 'libsmp-static.h'

STATIC_GEN_MAP = [
    ('src/buffer.h', 'SmpBuffer', 'SmpStaticBuffer'),
    ('src/context.h', 'SmpContext', 'SmpStaticContext'),
    ('src/libsmp-private.h', 'SmpMessage', 'SmpStaticMessage'),
    ('src/serial-protocol.h', 'SmpSerialProtocolDecoder',
        'SmpStaticSerialProtocolDecoder'),
    ]

STATIC_REP_LIST = [
    ('SmpBufferFreeFunc', 'void *'),
    ('SmpSerialProtocolDecoderState', 'int'),
    ('SmpSerialProtocolDecoder', 'void'),
    ('SmpSerialDevice', 'int'),
    ('SmpEventCallbacks cbs', 'void *cbs[2]'),
    ('SmpBuffer', 'void'),
    ('SmpMessage', 'void'),
    ]


parser = argparse.ArgumentParser()
parser.add_argument("filename", nargs='?', default=OUTPUT_NAME,
                    help="the output file name")
args = parser.parse_args()
OUTPUT_NAME = args.filename


def add_dir_to_zip(zip_file, path, keep_path):
    path = normpath(path)
    if isdir(path):
        for root, dirs, files in os.walk(path):
            for f in files:
                if f not in EXCLUDED_FILES:
                    if keep_path:
                        zip_file.write(join(root, f),
                                       normpath(join(basename(path),
                                                relpath(root, path), f)))
                    else:
                        zip_file.write(join(root, f), f)
    else:
        print(path, ": no such directory")


def generate_config_file(params):
    cf = tempfile.NamedTemporaryFile(mode='w', delete=False)
    cf.write("#pragma once\n\n")
    for param in params:
        # Ask user for configuration value
        print(param[0] + " [default=" + str(param[1]) + "]  " + param[2])
        try:
            value = int(input("> "))
        except ValueError:
            value = int(param[1])
        print(param[0], "=", value)
        print()
        cf.write("/* " + param[2] + " */\n")
        cf.write("#define " + param[0] + " " + str(value) + "\n\n")
    cf.close()
    return cf.name


def find_closing_brace(string, start):
    sub = string[start:]
    n_opbrace = 1
    pos = start
    for c in sub:
        if c == '{':
            n_opbrace += 1
        elif c == '}':
            n_opbrace -= 1

        if n_opbrace == 0:
            break

        pos += 1

    if n_opbrace:
        return -1

    return pos


def extract_struct_content(header_path, struct_name):
    with open(header_path) as fin:
        data = fin.read()
        res = re.search(r'struct ' + struct_name + '\s*({)\s*', data)
        start = res.end(1)
        end = find_closing_brace(data, start)
        if end == -1:
            print('no closing brace found in \'' + header_path
                  + '\' for struct \'' + struct_name + '\'')
            return ""

        return data[start:end]


def generate_static_file(base_dir):
    cf = tempfile.NamedTemporaryFile(mode='w', delete=False)
    cf.write('#pragma once\n\n'
             '#include <stdint.h>\n'
             '#include "libsmp.h"\n\n'
             '#ifdef __cplusplus\n'
             'extern "C" {\n'
             '#endif\n\n')

    for (header, struct_name, static_struct_name) in STATIC_GEN_MAP:
        hdr_path = join(base_dir, header)
        struct_content = extract_struct_content(hdr_path, struct_name)

        cf.write('typedef struct ' + static_struct_name + ' '
                 + static_struct_name + ';\n')
        cf.write('struct ' + static_struct_name + ' {\n')

        for (old, new) in STATIC_REP_LIST:
            struct_content = struct_content.replace(old, new)

        cf.write(struct_content)
        cf.write('};\n')

    cf.write('#ifdef __cplusplus\n'
             '}\n'
             '#endif')

    cf.close()
    return cf.name


base_dir = join(dirname(realpath(__file__)), "..")
output_file = zipfile.PyZipFile(OUTPUT_NAME, 'w')

for file in INCLUDED_FILES:
    full_path = normpath(join(base_dir, file))
    if isfile(full_path):
        output_file.write(full_path, basename(full_path))
    else:
        print(full_path, ": no such file")
for directory in INCLUDED_DIRS:
    add_dir_to_zip(output_file, join(base_dir, directory), True)
for directory in EXTRACTED_DIRS:
    add_dir_to_zip(output_file, join(base_dir, directory), False)
for config_filename, parameters in CONFIGURATION_PARAMETERS.items():
    tmp_config_file = generate_config_file(parameters)
    output_file.write(tmp_config_file, config_filename)
    os.remove(tmp_config_file)

static_file = generate_static_file(base_dir)
output_file.write(static_file, LIBSMP_STATIC_FILENAME)
os.remove(static_file)

output_file.close()
print("Arduino library generated:", join(os.getcwd(), OUTPUT_NAME))
