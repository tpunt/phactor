--TEST--
Stackful context switching on the Zend VM.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$as = new ActorSystem(1);

class A extends Actor
{
    public function __construct()
    {
        $this->send(ActorRef::fromActor($this), 1);
    }

    public function receive()
    {
        var_dump($this->receiveBlock()); // int(1)
        $this->send('B', ['A', 2]);
        var_dump($this->sub1()); // int(3)
        $this->send('B', 4);
    }

    private function sub1() : int
    {
        return $this->sub2();
    }

    private function sub2() : int
    {
        return $this->receiveBlock();
    }
}

class B extends Actor
{
    public function receive()
    {
        [$sender, $message] = $this->receiveBlock();
        var_dump($message); // int(2)
        $this->send($sender, 3);
        $this->sub1();
        ActorSystem::shutdown();
    }

    private function sub1() : void
    {
        $this->sub2();
    }

    private function sub2() : void
    {
        var_dump($this->receiveBlock()); // int(4)
    }
}

new ActorRef(B::class, [], 'B');
new ActorRef(A::class, [], 'A');
--EXPECT--
int(1)
int(2)
int(3)
int(4)
