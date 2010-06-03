--[[
	Hook Documentation Compiler
		By N3X15

	(GPL2 here)
]]--

Hooks={}
HookWiki=[[
== Documented Hooks ==

The following are hooks that have been documented in Luna (latest SVN).

{| class="awesometable"
|-
! Name ! Arguments ! File ! Description
]]

HookDocs=[[
-- AUTOMATICALLY GENERATED BY scripts/GetHooks.lua! DO NOT MODIFY! --

]]
function pairsByKeys (t, f)
	local a = {}
	for n in pairs(t) do table.insert(a, n) end
		table.sort(a, f)
		local i = 0      -- iterator variable
		local iter = function ()   -- iterator function
			i = i + 1
			if a[i] == nil then return nil
			else return a[i], t[a[i]]
		end
	end
	return iter
end

function ParseFile(fname)
        if fname == nil then return end
	local f = io.open(fname)
	i=0
	for line in f:lines() do
		i=i+1
		-- @hook ExampleHook(a,b,c) lol description
		local hookname,hookargs,hookdesc = line:match("@hook (%S+)%((.*)%) (.*)")
		if hookname then
			if Hooks[hookname]==nil then
				Hooks[hookname]={}
				Hooks[hookname].args=hookargs
				Hooks[hookname].file=string.format("indra/%s:%d",fname,i)
				Hooks[hookname].desc=hookdesc
			end
		end
	end
	f:close() 
end
for v in io.lines() do
	ParseFile(v)
end
table.sort(Hooks)
for k,v in pairs(Hooks) do
	print("[HOOK]",k,v["desc"])
	HookWiki=HookWiki..string.format("|-\n! [[%s]]\n| %s\n| %s\n| %s\n",k,v["args"],v["file"],v["desc"])
	HookDocs=HookDocs..string.format("\n-- defined in %s\nRegisterHook(\"%s\"\t,\"%s\")\n",v["file"],k,v["desc"])
end
HookWiki=HookWiki..[[
|-
|}

]]

f = io.open("HookDocs.wiki","w")
f:write(HookWiki)
f:close()

f = io.open("newview/lua/HookDocs.lua","w")
f:write(HookDocs)
f:close()