#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for Huawei ubs-io to install pkg
# version: 1.0.0
# ***********************************************************************

common_params=("\\" "\"" "!" "'" "~" "\`" "@" "#" "$" "%" "^" "&" "(" ")" "-" "_" "=" "+" "\|" "[" "{" "}" "]" ";"
  ":" "," "<" "." ">" "/" " " $'\t' $'\v' $'\f' $'\r' $'\n' $'\b')

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