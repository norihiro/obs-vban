obs = obslua
vbanout_id = "net.nagater.obs-vban.output"
vbanout = nil
mixer_last = -1

function vbanout_start(settings)
	print("Info: starting vbanout-lua...")
	if vbanout then
		print("Error: vbanout instance is already there.")
		return
	end

	vbanout = obs.obs_output_create(vbanout_id, "vbanout-lua", settings, nil)
	if not vbanout then
		print("Error: failed to create output '" .. vbanout_id .. "'")
		return
	end

	-- obs.obs_output_set_media(vbanout, nil, obs.obs_get_audio())
	if not obs.obs_output_start(vbanout) then
		print("Error: failed to start vbanout")
		vbanout_stop()
	end
end

function vbanout_stop()
	print("Info: Stopping vbanout-lua...")
	if not vbanout then
		return
	end

	obs.obs_output_stop(vbanout)
	obs.obs_output_release(vbanout)
	vbanout = nil
end

function script_update(settings)
	print("got settings")

	local autostart = obs.obs_data_get_bool(settings, "lua_autostart")
	local mixer = obs.obs_data_get_int(settings, "mixer")

	if (not autostart or mixer ~= mixer_last) and vbanout then
		vbanout_stop()
	end

	if vbanout then
		obs.obs_output_update(vbanout, settings)
	elseif autostart then
		vbanout_start(settings)
		mixer_last = mixer
	end
end

function script_properties()
	local props = obs.obs_get_output_properties(vbanout_id)
	if props == nil then
		print("Cannot find output '" .. vbanout_id .. "'")
		return nil
	end

	-- obs.obs_properties_add_button(props, "vbanout_lua_start_stop", "Start/stop", vbanout_start_stop_button)
	obs.obs_properties_add_bool(props, "lua_autostart", "Start/stop")
	return props
end

function script_defaults(settings)
	local d = obs.obs_output_defaults(vbanout_id)
	local nn = {'port', 'mixer', 'format_bit'}
	for k, n in pairs(nn) do
		obs.obs_data_set_default_int(settings, n, obs.obs_data_get_int(d, n))
	end
	obs.obs_data_release(d)
end

function script_description()
	return "Start VBAN audio output"
end

function script_load(settings)
	print("script_load...")
end

function script_unload()
	print("script_unload...")
	if vbanout then
		vbanout_stop()
	end
end
