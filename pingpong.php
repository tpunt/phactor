<?php

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function receive($sender, $message)
    {
        var_dump("({$message[1]}) {$message[0]}");

        $this->send($sender, ['pong', $message[1] + 1]);
    }
}

new class (new Test, 1) extends Actor {
    function __construct(Test $test, int $n)
    {
        $this->send($test, ['ping', $n]);
    }

    function receive($sender, $message)
    {
        var_dump("({$message[1]}) {$message[0]}");

        if ($message[1] < 10) {
            $this->send($sender, ['ping', $message[1] + 1]);
        }
    }
};

$actorSystem->block();
