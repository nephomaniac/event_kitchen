#!/usr/bin/env/ lua

--#!/usr/bin/lua
package.path = package.path .. ";/usr/lib/orange/lib/?.lua"
package.path = package.path .. ";/usr/lib/orange/lib/orange/?.lua"
-- package.path = package.path .. ";/usr/lib/orange/api/juci/smartrg.?.lua"

local ubus = require("ubus");
local uloop = require("uloop");
local json = require("json");

uloop.init();

local conn = ubus.connect();

if not conn then 
    error("Failed to connect to ubus");
end

local msg_callback = function(msg)
    print("GOT UBUS MESSAGE:"..json.encode(msg).."\n");
end

-- Creat subscriber obj with function/callback. (defined inline here) 
local sub = {
    notify = msg_callback,
}

-- Now subscribe....
conn:subscribe("test_topic", sub); 

uloop.run();

