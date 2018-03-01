--TEST--
Stackless context switching on the Zend VM.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem(1);

$ar = new ActorRef(Test::class, [], 'test');
new ActorRef(Test2::class, [$ar], 'test2');

class Test extends Actor
{
    public function receive()
    {
        [$sender, $message] = $this->receiveBlock();
        var_dump($message);
        $this->send($sender, [ActorRef::fromActor($this), 2]);
        var_dump($this->receiveBlock());
        var_dump($message);
        $this->send($sender, 4);
    }
}

class Test2 extends Actor
{
    public function __construct(ActorRef $ar)
    {
        $this->send($ar, [ActorRef::fromActor($this), 1]);
    }

    public function receive()
    {
        [$sender, $message] = $this->receiveBlock();
        var_dump($message);
        $this->send($sender, 3);
        var_dump($this->receiveBlock());
        var_dump($message);
        ActorSystem::shutdown();
    }
}
--EXPECT--
int(1)
int(2)
int(3)
int(1)
int(4)
int(2)
