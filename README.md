*powerman* is free UNIX/Linux software that controls (remotely and in
parallel) switched power distribution units.  It was designed for remote
power control of Linux systems in a data center or cluster environment,
but has been used in other environments such as embedded management
appliances, home automation, and high availability service management.

*powerman* can be extended to support new devices using an expect-like
scripting language.  It communicates with devices natively using telnet,
raw socket, and serial protocols.  It also can drive virtual power control
devices via a coprocess interface.  The coprocess mechanism has been used
to extend *powerman* to communicate with devices using other protocols
such as SNMP, IPMI, Insteon, X-10, and VXI-11.

*powerman* can control equipment connected using any combination of the
above methods and provide unified naming for the equipment and parallel
execution of control actions. 
