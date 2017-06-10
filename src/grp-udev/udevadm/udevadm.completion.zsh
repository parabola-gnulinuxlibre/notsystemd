#compdef udevadm

_udevadm_info(){
    _arguments \
        '--query=[Query the database for specified type of device data. It needs the --path or --name to identify the specified device.]:type:(name symlink path property all)' \
        '--path=[The devpath of the device to query.]:sys files:_files -P /sys/ -W /sys' \
        '--name=[The name of the device node or a symlink to query]:device files:_files -P /dev/ -W /dev' \
        '--root[Print absolute paths in name or symlink query.]' \
        '--attribute-walk[Print all sysfs properties of the specified device that can be used in udev rules to match the specified device]' \
        '--export[Print output as key/value pairs.]' \
        '--export-prefix=[Add a prefix to the key name of exported values.]:prefix' \
        '--device-id-of-file=[Print major/minor numbers of the underlying device, where the file lives on.]:files:_udevadm_mounts' \
        '--export-db[Export the content of the udev database.]' \
        '--cleanup-db[Cleanup the udev database.]'
}

_udevadm_trigger(){
    _arguments \
        '--verbose[Print the list of devices which will be triggered.]' \
        '--dry-run[Do not actually trigger the event.]' \
        '--type=[Trigger a specific type of devices.]:types:(devices subsystems failed)' \
        '--action=[Type of event to be triggered.]:actions:(add change remove)' \
        '--subsystem-match=[Trigger events for devices which belong to a matching subsystem.]' \
        '--subsystem-nomatch=[Do not trigger events for devices which belong to a matching subsystem.]' \
        '--attr-match=attribute=[Trigger events for devices with a matching sysfs attribute.]' \
        '--attr-nomatch=attribute=[Do not trigger events for devices with a matching sysfs attribute.]' \
        '--property-match=[Trigger events for devices with a matching property value.]' \
        '--tag-match=property[Trigger events for devices with a matching tag.]' \
        '--sysname-match=[Trigger events for devices with a matching sys device name.]' \
        '--parent-match=[Trigger events for all children of a given device.]'
}

_udevadm_settle(){
    _arguments \
       '--timeout=[Maximum number of seconds to wait for the event queue to become empty.]' \
       '--seq-start=[Wait only for events after the given sequence number.]' \
       '--seq-end=[Wait only for events before the given sequence number.]' \
       '--exit-if-exists=[Stop waiting if file exists.]:files:_files' \
       '--quiet[Do not print any output, like the remaining queue entries when reaching the timeout.]' \
       '--help[Print help text.]'
}

_udevadm_control(){
    _arguments \
        '--exit[Signal and wait for systemd-udevd to exit.]' \
        '--log-priority=[Set the internal log level of systemd-udevd.]:priorities:(err info debug)' \
        '--stop-exec-queue[Signal systemd-udevd to stop executing new events. Incoming events will be queued.]' \
        '--start-exec-queue[Signal systemd-udevd to enable the execution of events.]' \
        '--reload[Signal systemd-udevd to reload the rules files and other databases like the kernel module index.]' \
        '--property=[Set a global property for all events.]' \
        '--children-max=[Set the maximum number of events.]' \
        '--timeout=[The maximum number of seconds to wait for a reply from systemd-udevd.]' \
        '--help[Print help text.]'
}

_udevadm_monitor(){
    _arguments \
        '--kernel[Print the kernel uevents.]' \
        '--udev[Print the udev event after the rule processing.]' \
        '--property[Also print the properties of the event.]' \
        '--subsystem-match=[Filter events by subsystem/\[devtype\].]' \
        '--tag-match=[Filter events by property.]' \
        '--help[Print help text.]'
}

_udevadm_test(){
    _arguments \
        '--action=[The action string.]:actions:(add change remove)' \
        '--subsystem=[The subsystem string.]' \
        '--help[Print help text.]' \
        '*::devpath:_files -P /sys/ -W /sys'
}

_udevadm_test-builtin(){
    if (( CURRENT == 2 )); then
    _arguments \
        '--help[Print help text]' \
        '*::builtins:(blkid btrfs hwdb input_id net_id net_setup_link kmod path_id usb_id uaccess)'
    elif  (( CURRENT == 3 )); then
        _arguments \
            '--help[Print help text]' \
            '*::syspath:_files -P /sys -W /sys'
    else
        _arguments \
            '--help[Print help text]'
    fi
}

_udevadm_mounts(){
  local dev_tmp dpath_tmp mp_tmp mline

    tmp=( "${(@f)$(< /proc/self/mounts)}" )
    dev_tmp=( "${(@)${(@)tmp%% *}:#none}" )
    mp_tmp=( "${(@)${(@)tmp#* }%% *}" )

  local MATCH
  mp_tmp=("${(@q)mp_tmp//(#m)\\[0-7](#c3)/${(#)$(( 8#${MATCH[2,-1]} ))}}")
  dpath_tmp=( "${(@Mq)dev_tmp:#/*}" )
  dev_tmp=( "${(@q)dev_tmp:#/*}" )

  _alternative \
    'device-paths: device path:compadd -a dpath_tmp' \
    'directories:mount point:compadd -a mp_tmp'
}


_udevadm_command(){
    local -a _udevadm_cmds
    _udevadm_cmds=(
        'info:query sysfs or the udev database'
        'trigger:request events from the kernel'
        'settle:wait for the event queue to finish'
        'control:control the udev daemon'
        'monitor:listen to kernel and udev events'
        'test:test an event run'
        'test-builtin:test a built-in command'
    )

    if ((CURRENT == 1)); then
        _describe -t commands 'udevadm commands' _udevadm_cmds
    else
        local curcontext="$curcontext"
        cmd="${${_udevadm_cmds[(r)$words[1]:*]%%:*}}"
        if (($#cmd)); then
            if (( $+functions[_udevadm_$cmd] )); then
                _udevadm_$cmd
            else
                _message "no options for $cmd"
            fi
        else
            _message "no more options"
        fi
    fi
}


_arguments \
    '--debug[Print debug messages to stderr]' \
    '--version[Print version number]' \
    '--help[Print help text]' \
    '*::udevadm commands:_udevadm_command'