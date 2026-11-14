#!/usr/bin/env python3
# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# check_abi.py - Compare S31 radio callback layouts with independent SDK headers.
# SPDX-License-Identifier: Apache-2.0
"""Compile ABI probes without linking or executing the vendor libraries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf', type=Path, required=True)
    parser.add_argument('--cc', default='riscv32-esp-elf-gcc')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    base = Path(__file__).resolve().parent
    wifi = args.idf.resolve() / 'components/esp_wifi/include/esp_private/wifi_os_adapter.h'
    bt = args.idf.resolve() / 'components/bt/porting_btdm/controller/btdm_common/include/btdm_osal.h'
    flags = [args.cc, '-march=rv32imafc_zicsr_zifencei', '-mabi=ilp32f', '-Werror',
             '-DPLATFORM_ESP32S31=1']
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        (work / 'sdkconfig.h').write_text('#define CONFIG_BT_CTRL_MULTI_LINK_ENABLED 1\n'
            '#define CONFIG_IDF_TARGET_ESP32S31 1\n#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n')
        flags += ['-I' + str(work)]
        pre = subprocess.check_output(flags + ['-E', '-P', '-include', str(wifi),
                                               '-xc', '/dev/null'], text=True)
        fields = ['_version'] + re.findall(r'\(\*\s*(_\w+)\)', pre) + ['_magic']
        def values(name, header, expressions):
            source = work / (name + '.c')
            assembly = source.with_suffix('.s')
            source.write_text('#include <stddef.h>\n#include "' + str(header) +
                '"\nconst unsigned abi[]={' + ','.join(expressions) + '};\n')
            subprocess.run(flags + ['-S', str(source), '-o', str(assembly)], check=True)
            result = [int(v) for v in re.findall(r'\.word\s+(\d+)', assembly.read_text())]
            if len(result) != len(expressions):
                raise ValueError('compiler ABI output length: ' + name)
            return result
        expressions = ['sizeof(wifi_osi_funcs_t)'] + [
            'offsetof(wifi_osi_funcs_t,' + f + ')' for f in fields]
        native = values('wifi-native', base.parent / 'esp_abi.h', expressions)
        if native != values('wifi-vendor', wifi, expressions):
            raise ValueError('Wi-Fi OSI layout differs from SDK')
        handles = ['event', 'eventq', 'callout', 'mutex', 'sem']
        errors = ['OK', 'ENOMEM', 'EINVAL', 'INVALID_PARM', 'MEM_NOT_ALIGNED',
                  'BAD_MUTEX', 'TIMEOUT', 'ERR_IN_ISR', 'ERR_PRIV', 'NOT_STARTED',
                  'ENOENT', 'EBUSY', 'ERROR']
        def bt_expressions(prefix, counts, error_prefix):
            return ([op + '(struct ' + prefix + h + ')' for h in handles
                     for op in ('sizeof', '_Alignof')] + ['sizeof(' + counts + ')'] +
                    ['offsetof(' + counts + ',' + f + ')' for f in
                     ('evt_count', 'evtq_count', 'co_count', 'sem_count', 'mutex_count')] +
                    [error_prefix + ({"INVALID_PARM": "INVALID_PARAM",
                      "NOT_STARTED": "OS_NOT_STARTED"}.get(e, e)
                      if error_prefix == "BLE_NPL_" else e) for e in errors])
        ble = values('ble-native', base / 'btdm_s31_osal.h',
                     bt_expressions('ble_npl_', 'espb_npl_counts_t', 'BLE_NPL_'))
        if ble != values('ble-vendor', bt,
                         bt_expressions('btdm_osal_', 'btdm_osal_elem_num_t', 'BTDM_OSAL_')):
            raise ValueError('BLE OSAL layout or errors differ from SDK')
        flags += ['-I' + str(args.idf.resolve() / 'components' / component / 'include')
                  for component in ('esp_event', 'esp_common')]
        types = args.idf.resolve() / 'components/esp_wifi/include/esp_wifi_types_generic.h'
        contracts = ['sizeof(' + t + ')' for t in ('wifi_scan_config_t', 'wifi_ap_record_t',
            'wifi_sta_config_t', 'wifi_config_t', 'wifi_event_sta_connected_t',
            'wifi_event_sta_disconnected_t')]
        contracts += ['offsetof(wifi_sta_config_t,' + f + ')' for f in
                      ('password', 'scan_method', 'listen_interval', 'threshold', 'pmf_cfg', 'sae_pwe_h2e')]
        contracts += ['sizeof(((wifi_config_t *)0)->ap)']
        contracts += ['offsetof(wifi_config_t,ap.' + f + ')' for f in
                      ('password', 'channel', 'authmode', 'max_connection', 'beacon_interval',
                       'pairwise_cipher', 'pmf_cfg', 'sae_pwe_h2e', 'gtk_rekey_interval')]
        station = values('station-native', base.parent / 'esp_abi.h', contracts)
        vendor_station = values('station-vendor', types, contracts)
        if station != vendor_station:
            raise ValueError('station ABI differs: ' + str(list(zip(contracts, station, vendor_station))))
        pool = bt.with_name('btdm_mempool.h')
        def pool_expressions(prefix, native):
            fields = ['mp_block_size', 'mp_num_blocks', 'mp_num_free', 'mp_min_free',
                      'mp_flags', 'mp_membuf_addr', 'mp_next' if native else 'mp_list.stqe_next',
                      'mp_first' if native else 'slh_first', 'name']
            return (['sizeof(struct ' + prefix + 'mempool)',
                     'sizeof(struct ' + prefix + 'mempool_ext)'] +
                    ['offsetof(struct ' + prefix + 'mempool,' + f + ')' for f in fields] +
                    ['offsetof(struct ' + prefix + 'mempool_ext,' + f + ')' for f in
                     ('mpe_mp', 'mpe_put_cb', 'mpe_put_arg', 'mpe_get_cb', 'mpe_get_arg')])
        pool_layout = values('pool-native', base / 'btdm_s31_mempool.h',
                             pool_expressions('os_', True))
        if pool_layout != values('pool-vendor', pool, pool_expressions('btdm_', False)):
            raise ValueError('BTDM fixed-block pool layout differs from SDK')
        pool_symbols = set(re.findall(r'\b(e_btdm_\w+)\s*\(',
                                      (base / 'btdm_s31_mempool.h').read_text()))
        pool_declarations = subprocess.check_output(flags + ['-E', '-P', '-include', str(pool),
                                                             '-xc', '/dev/null'], text=True)
        pool_declarations = [d.strip() + ';' for d in pool_declarations.split(';')
                             if any(re.search(r'\b' + name + r'\s*\(', d) for name in pool_symbols)]
        if len(pool_declarations) != len(pool_symbols):
            raise ValueError('SDK does not declare every implemented BTDM pool callback')
        pool_prototypes = '\n'.join(pool_declarations)
        for old, new in {'btdm_mempool_ext': 'os_mempool_ext', 'btdm_mempool': 'os_mempool',
                         'btdm_osal_error_t': 'os_error_t'}.items():
            pool_prototypes = re.sub(r'\b' + old + r'\b', new, pool_prototypes)
        pool_source = work / 'pool-signatures.c'
        pool_source.write_text('#include "' + str(base / 'btdm_s31_mempool.h') +
                               '"\n' + pool_prototypes + '\n')
        subprocess.run(flags + ['-fsyntax-only', str(pool_source)], check=True)
        native_header = base / 'btdm_s31_osal.h'
        symbols = set(re.findall(r'\b(wr_btdm_osal_\w+)\s*\(', native_header.read_text()))
        declarations = subprocess.check_output(flags + ['-E', '-P', '-include', str(bt),
                                                         '-xc', '/dev/null'], text=True)
        declarations = [d.strip() + ';' for d in declarations.split(';')
                        if any(re.search(r'\b' + name + r'\s*\(', d) for name in symbols)]
        if len(declarations) != len(symbols):
            raise ValueError('SDK does not declare every implemented BLE callback')
        aliases = {'btdm_osal_time_t': 'uint32_t', 'btdm_osal_error_t': 'int',
                   'btdm_osal_task_handle_t': 'void *', 'btdm_osal_intr_handle_t': 'void *',
                   'btdm_osal_malloc_flag_t': 'uint32_t',
                   'btdm_osal_event_fn': 'ble_npl_event_fn'}
        aliases.update({'btdm_osal_' + h: 'ble_npl_' + h for h in handles})
        prototypes = '\n'.join(declarations)
        for old, new in aliases.items():
            prototypes = re.sub(r'\b' + old + r'\b', new, prototypes)
        source = work / 'signatures.c'
        source.write_text('#include "' + str(native_header) + '"\n'
                          'typedef void btdm_osal_task_fn(void *);\n'
                          'typedef void btdm_osal_intr_fn(void *);\n' + prototypes + '\n')
        subprocess.run(flags + ['-fsyntax-only', str(source)], check=True)
        report = dict(wifi_size=native[0], wifi_offsets=dict(zip(fields, native[1:])),
                      ble_layout_and_errors=ble, ble_signatures=len(symbols),
                      btdm_pool_layout=pool_layout, btdm_pool_signatures=len(pool_symbols),
                      station_abi=dict(zip(contracts, station)), inputs={str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in (wifi, bt, types, pool)})
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(f'PASS Wi-Fi {native[0]} bytes/{len(fields)} offsets; BLE handles, pools and errors')


if __name__ == '__main__':
    main()
