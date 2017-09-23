--TEST--
Support for closures
--DESCRIPTION--
Enable closures to be passed as messages, as well as be assigned as properties
to Actor objects
--FILE--
<?php

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function receive($sender, $message)
    {
        var_dump($message());
        $this->send($sender, $message);
    }
}

$actor = new class (new Test) extends Actor {
    function __construct(Test $test)
    {
        $this->func = function () {
            return "abc";
        };
        $this->send($test, $this->func);
    }

    function receive($sender, $message)
    {
        var_dump($message());
        var_dump(($this->func)());
    }
};

var_dump(($actor->func)());

$actorSystem->block();
--EXPECT--
string(3) "abc"
string(3) "abc"
string(3) "abc"
string(3) "abc"
