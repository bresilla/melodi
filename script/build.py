Import("env")

# nodeid in ipv6 format
ipv6_first = 65
ipv6_last = 65

# env.Append(CCFLAGS=["MY_NODE_ID=32"])
env.Append(CPPDEFINES=["IPV6_FIRST=0x%x" % ipv6_first, "IPV6_LAST=0x%x" % ipv6_last])
