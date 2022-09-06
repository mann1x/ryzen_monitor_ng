# ryzen_monitor(_ng) (NextGeneration)
Monitor power information of Ryzen processors via the PM table of the SMU.

Export metrics to telegraf/influxdb for monitoring.

SMU Set and Get for many parameters and CO counts.

I really needed something practical for monitoring and for scripting. Ended up doing so many and massive changes that I decided to branch out from the original. Hope you'll enjoy!

This tool is based on the [ryzen_monitor](https://github.com/hattedsquirrel/ryzen_monitor) which was based on the [ryzen_smu](https://gitlab.com/leogx9r/ryzen_smu) kernel module demo-tool.

Check here the [CHANGELOG](CHANGELOG.md).

**ryzen_monitor** features support for multiple PM table versions (i.e. multiple bios versions), adds support for Ryzen 5000, and presents more fields to user. It is especially focused around providing a more realistic image of the actual power draw and hence true thermal output of the CPU package.

## Usage
While in monitor mode you can interact with the following keys:

q - quit

c - toggle compact mode

i - toggle information pane

c - toggle co counts pane

e - toggle electrical pane

m - toggle memory pane

g - toggle graphics pane

p - toggle power pane

You can get a quick description of the command line options with the switch -h.

Many Set and Get commands are not dependent on a supported PM table.
If your codename is supported but the PM table not required, that operation will work anyway.

## Supported CPUs
* Ryzen 5000 series
  * Ryzen 9 5950X
  * Ryzen 9 5900X
  * Ryzen 7 5800X
  * Ryzen 5 5600X 
  * Ryzen 7 5700G
  * Ryzen 5 5600G
* Ryzen 3000 series
  * Ryzen 7 3700X
  * Ryzen 9 3900X
  * Ryzen 9 3950X
* Ryzen Picasso
  * Ryzen 7 3750H
* Ryzen Lucienne 
  * Ryzen 7 PRO 4750U
* Other non-Threadripper models will probably work, but are untested

Note: Support also depends on the PM table version that ships with your BIOS and whether ryzen_smu/ryzen_monitor already knows how to read it.

## Future developments
* autopilot mode; switch profiles (PBO, etc) based on time/load/conditions
* tune CO counts; test and seek for lowest CO count
* test for system/cores stability
* test for optimal PBO limits
* add more SMU comnmands
* add more PM tables

## Bugs, features and missing platforms

Please open an issue on Github.

If your CPU is not supported use the script /scripts/dump_pm_table2.py.

Note: You need pigz to run dump_pm_table2.py

Attach the PM table dumps to the issue. 

If you have an APU please also make a run while stressing the GPU. I have included clgpustress in the scripts folder. You need a working setup of OpenCL to use it. Run it in another session with "./gpustress-cli -A" and re-launch the dump script after renaming or archiving the folder with the dumps without GPU load.

## Screenshot
![ryzen monitor](https://user-images.githubusercontent.com/20623405/187942949-34b327d0-642f-4a4e-8fff-dfc5d4abbbee.png)

## Building
First install the kernel module from [mann1x:ryzen_smu](https://gitlab.com/mann1x/ryzen_smu) (not merged with the official one yet).
A good indication of whether the above module works for you is by checking whether `/sys/kernel/ryzen_smu_drv/pm_table` is available.

Then pull and make **ryzen_monitor**:
```bash
git clone https://github.com/mann1x/ryzen_monitor_ng.git
cd ryzen_monitor_ng/
make install

ryzen_monitor
```
Enjoy!

There's also the debug target available.

## telegraf configuration

Create a systemd.unit file (/etc/systemd/system/ryzen_monitor.service):

```s
[Unit]
Description=ryzen_monitor
After=network.target
StartLimitIntervalSec=0

[Service]
Type=simple
Restart=always
RestartSec=20
User=root
ExecStart=/usr/local/bin/ryzen_monitor -e/tmp/ryzen_monitor_export
SyslogIdentifier=ryzen_monitor
LogLevelMax=err

[Install]
WantedBy=multi-user.target
```

Configure in your telegraf.conf file the tail plugin:

```s
[[inputs.tail]]
   files = ["/tmp/ryzen_monitor_export"]
   from_beginning = false
   pipe = true
   watch_method = "poll"
   data_format = "influx"
```

Then you can create beautiful dashboards in Chronograf:

![chronograf](https://user-images.githubusercontent.com/20623405/188276670-a8a9ba2c-6bf9-48d6-ae0c-699cc62ade1a.png)


Thanks to the named pipe output and tail input you can keep telegraf running every 30 seconds (or more) and get ryzen_monitor metrics with a 10 seconds granularity. The load is well below the threshold to interfere with the CPU power saving states.

The update time for the refresh can be set using the switch -u in the unit ExecStart command line.

The stats will be available under the tree telegraf.autogen -> ryzen_monitor_ng.

## About the quality of the provided information
Don't rely on the information given by this tool.

To my knowledge there is no official documentation on how to do this. Everything was created by starring at numbers, a lot of guesswork and finding fragments somewhere on the web. It is possible that some assignments or calculations are incorrect.

This program is provided as is. If anything, it is a toy for the curious. Nothing more. Use it at your own risk.

(This statement still holds true, there's a bit more confidence the metrics are right but don't trust them blindly)