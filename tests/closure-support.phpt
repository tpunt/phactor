--TEST--
Support for closures
--DESCRIPTION--
Enable closures to be passed as messages
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function receive()
    {
        $message = $this->receiveBlock();
        var_dump($message());
        $this->send('test2', $message);
    }
}

class Test2 extends Actor
{
    function __construct()
    {
        $this->func = function () {
            return "abc";
        };
        $this->send('test', $this->func);
    }

    function receive()
    {
        var_dump($this->receiveBlock()());
        var_dump(($this->func)());
        ActorSystem::shutdown();
    }
}

new ActorRef(Test::class, [], 'test');
new ActorRef(Test2::class, [], 'test2');
--EXPECT--
string(3) "abc"
string(3) "abc"
string(3) "abc"
