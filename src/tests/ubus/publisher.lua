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

-- This is the function that will get called back when we get a subscriber...
local sub_callback = function(subs)
    print("GOT TOTAL SUBS:", subs);
end

-- Creat subscriber obj with function/callback. (defined inline here) 
-- The sub-objects within this object seem to represent topics we'll handle here...
local ubus_namespace_objs = {
    test_topic = {
        __subscriber_cb = sub_callback
    }
}

conn:add(ubus_namespace_objs);
-- Now do the publishing....
local timer;
local counter = 0;
local t = function()
    counter = counter + 1;
    -- Publish here...
    conn.notify(ubus_namespace_objs.test_topic.__ubuseobj, "test_topic.counter", {count= counter, words = "Some string stuff"});
    timer:set(1000);
end
timer = uloop.timer(t); 
timer:set(1000);

uloop.run();

