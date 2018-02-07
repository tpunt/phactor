--TEST--
Basics 1 test - ping pong
--DESCRIPTION--
Test basic message passing
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn};

$actorSystem = new ActorSystem(true);

class Test extends Actor
{
    public function receive($sender, $message)
    {
        var_dump("({$message[1]}) {$message[0]}");

        $this->send($sender, ['pong', $message[1] + 1]);
    }
}

class Test2 extends Actor {
    function __construct(int $n)
    {
        $this->send('test', ['ping', $n]);
    }

    function receive($sender, $message)
    {
        var_dump("({$message[1]}) {$message[0]}");

        if ($message[1] < 10) {
            $this->send($sender, ['ping', $message[1] + 1]);
        } else {
            ActorSystem::shutdown();
        }
    }
};

spawn('test', Test::class);
spawn('test2', Test2::class, 1);
--EXPECT--
string(8) "(1) ping"
string(8) "(2) pong"
string(8) "(3) ping"
string(8) "(4) pong"
string(8) "(5) ping"
string(8) "(6) pong"
string(8) "(7) ping"
string(8) "(8) pong"
string(8) "(9) ping"
string(9) "(10) pong"
