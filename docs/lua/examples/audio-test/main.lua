-- name: Audio Test
-- description: Testing out the custom audio system

local audioStream, audioSample

function on_stream_play(msg)
    if msg == "load" then
        audioStream = audio_load("music.mp3", MA_TYPE_STREAM)
        audioStream.looping = true
        djui_chat_message_create("audio audioStream: " .. tostring(audioStream))
    elseif not audioStream then
        djui_chat_message_create("load the stream first!")
        return true

    elseif msg == "play" then
        audioStream.position = 0
        audioStream:play()
        djui_chat_message_create("playing audio")
    elseif msg == "resume" then
        audioStream:play()
        djui_chat_message_create("resuming audio")
    elseif msg == "pause" then
        audioStream:pause()
        djui_chat_message_create("pausing audio")
    elseif msg == "stop" then
        audioStream:stop()
        djui_chat_message_create("stopping audio")
    elseif msg == "destroy" then
        audioStream:destroy()
        djui_chat_message_create("destroyed audio")
    elseif msg == "getpos" then
        djui_chat_message_create("pos: " .. tostring(audioStream.position))
    end

    return true
end

function on_sample_play(msg)
    if msg == "load" then
        audioSample = audio_load("sample.mp3", MA_TYPE_SAMPLE)
        djui_chat_message_create("audio audioSample: " .. tostring(audioSample))
        return true
    elseif not audioSample then
        djui_chat_message_create("load the sample first!")
        return true
    end

    audioSample:play(gMarioStates[0].pos, 1)
    return true
end

hook_chat_command('stream', "[load|play|resume|pause|stop|destroy|getpos]", on_stream_play)
hook_chat_command('sample', "[load|play]", on_sample_play)
