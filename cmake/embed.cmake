# embed_file(<output .c> <input file> <symbol>): the file as "const unsigned char <symbol>[]"
# plus "const unsigned int <symbol>_len", generated at configure time.
function(embed_file output input symbol)
    file(READ ${input} hex HEX)
    string(LENGTH "${hex}" hex_len)
    math(EXPR size "${hex_len} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
    file(WRITE ${output}
        "/* Generated from ${input} */\n"
        "const unsigned char ${symbol}[] = {\n${bytes}\n};\n"
        "const unsigned int ${symbol}_len = ${size};\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${input})
endfunction()
