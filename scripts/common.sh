#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for Huawei hyperio to build pkg
# version: 1.0.0
# change log:
# ***********************************************************************

common_params=("\\" "\"" "!" "'" "~" "\`" "@" "#" "$" "%" "^" "&" "(" ")" "-" "_" "=" "+" "\|" "[" "{" "}" "]" ";" ":" "," "<" "." ">" "/" " ")

transfor_special_characters()
{
    local input_params=$1
    out_params=$input_params
    for((i=0;i<${#common_params[@]};i++))
    do
        out_params=${out_params//${common_params[i]}/\\${common_params[i]}}
    done
    out_params=${out_params//\?/\\?}
    echo -e "${out_params}"
    return $?
}