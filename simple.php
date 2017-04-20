<?php

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function receive($sender, $message)
    {
        $this->send($sender, $message + 10);
    }
}

new class (new Test, 1) extends Actor {
    public function __construct($test, $n)
    {
        for ($i = 0; $i < 10; ++$i) {
            $this->send($test, $i);
        }
    }

    public function receive($sender, $message)
    {
        var_dump($message);
    }
};

$actorSystem->block();
