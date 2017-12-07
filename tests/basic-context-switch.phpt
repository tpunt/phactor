--TEST--
Allow for Actors to be properties
--DESCRIPTION--
Allow for Actor-based objects to be assigned as properties of other Actor-based
objects.
--FILE--
<?php

$actorSystem = new ActorSystem();

$a = new class extends Actor {
    public function receive($sender, $message)
    {
        var_dump($message);
        $this->send($sender, 2);
        var_dump($this->receiveBlock());
        var_dump($message);
        $this->send($sender, 4);
    }
};

$b = new class($a) extends Actor {
    private $workers;

    public function __construct(Actor $to)
    {
        $this->send($to, 1);
    }

    public function receive($sender, $message)
    {
        var_dump($message);
        $this->send($sender, 3);
        var_dump($this->receiveBlock());
        var_dump($message);
    }
};

$actorSystem->block();
--EXPECT--
int(1)
int(2)
int(3)
int(1)
int(4)
int(2)
