--TEST--
Support for closures
--DESCRIPTION--
Enable closures to be passed as messages
--FILE--
<?php

$actorSystem = new ActorSystem(true);

class Test extends Actor
{
    public function receive($sender, $message)
    {
        var_dump($message());
        $this->send($sender, $message);
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

    function receive($sender, $message)
    {
        var_dump($message());
        var_dump(($this->func)());
        ActorSystem::shutdown();
    }
}

spawn('test', Test::class);
spawn('test2', Test2::class);

$actorSystem->block();
--EXPECT--
string(3) "abc"
string(3) "abc"
string(3) "abc"
