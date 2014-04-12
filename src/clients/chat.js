"use strict";

$(function() {
    var hostname = window.location.hostname;
    var port = "9001";
    var webSocket = null;

    function openWebSocket(ws_uri)
    {
        if ("WebSocket" in window)
        {
            // Chrome, IE10
            webSocket = new WebSocket(ws_uri, "chatserver");
        }
        else if ("MozWebSocket" in window)
        {
            // Firefox 7-10 (currently vendor prefixed)
            webSocket = new MozWebSocket(ws_uri, "chatserver");
        }
        else
        {
            throw "neither WebSocket nor MozWebSocket available";
        }

        return webSocket;
    }

    function addRoomsToTable(rooms)
    {
        var obj = $("#rooms").find("tbody").empty();
        for (var i = 0; i < rooms.length; i++)
        {
            if (webSocket.roomName == rooms[i])
            {
                obj = obj.append($("<tr>").append($("<td>").text(rooms[i])).append($("<td>")));
            }
            else
            {
                obj = obj.append($("<tr>").append($("<td>").text(rooms[i])).append($("<td>").append($("<button>").attr("id", "join_button").html("Join"))));
            }
        }
        $("#join_button").click(joinRoom);
        if (rooms.length > 0)
        {
            $("#rooms").show();
        }
    }

    function addClientsToTable(clients)
    {
        var obj = $("#clients").find("tbody").empty();
        for (var i = 0; i < clients.length; i++)
        {
            obj = obj.append($("<tr>").append($("<td>").text(clients[i])));
        }
        if (clients.length > 0)
        {
            $("#clients").show();
        }
    }

    function chatStuff()
    {
        var ws_uri = "ws://" + hostname + ":" + port + "/";

        webSocket = openWebSocket(ws_uri);
        webSocket.binaryType = "arraybuffer";

        webSocket.onopen = function(e)
        {
            var stringified = JSON.stringify({"request":{"value":"get_rooms"}});
            webSocket.send(stringified);
        }

        webSocket.onclose = function(e)
        {
           console.log(e.reason);
        }

        webSocket.onerror = function(e)
        {
        }

        webSocket.onmessage = function(e)
        {
            var data = JSON.parse(e.data);

            if ('joined_room' in data)
            {
                webSocket.roomName = data.joined_room;
                $("#room_name").text("Room: " + webSocket.roomName);
            }

            if ('rooms' in data)
            {
                addRoomsToTable(data.rooms);
            }

            if ('message' in data)
            {
                var msg = data.message;
                $("#chatbox").append(msg.client + ": " + msg.value + "&#10;");
            }

            if ('clients' in data)
            {
                addClientsToTable(data.clients);
            }
        }
    }

    function createRoom()
    {
        var newRoom = $("#room").val();
        var userName = $("#name").val 

        if (newRoom.length > 0 && userName.length > 0)
        {
            var stringified = JSON.stringify({"request":{"value":"create_room","room":newRoom,"user_name":userName}});
            webSocket.send(stringified);
        }
    }

    function joinRoom()
    {
        var room = $(this).parent().prev().text();
        var userName = $("#name").val();

        if (room.length > 0 && userName.length > 0)
        {
            var stringified = JSON.stringify({"request":{"value":"join_room","room":room,"user_name":userName}});
            webSocket.send(stringified);
        }
    }

    function sendMessage()
    {
        webSocket.send(JSON.stringify({"message":$("#sendbox").val()}));
        $("#sendbox").val("");
    }

    $("#rooms").hide();
    $("#clients").hide();
    $("#create_button").click(createRoom);
    $("#sendbox").keyup(function(e) { if (e.keyCode == 13) sendMessage(); });
    chatStuff();
});

