counter = 0

function setup(thread)
    thread:set("id", counter)
    counter = counter + 1
end

function init(args)
    tid = id
    seq = 0
    headers = {}
    headers["Content-Type"] = "application/x-www-form-urlencoded"
end

request = function()
    seq = seq + 1
    local username = "bench_user_" .. tid .. "_" .. seq
    local body = "username=" .. username .. "&password=123456"
    return wrk.format("POST", "/register", headers, body)
end
