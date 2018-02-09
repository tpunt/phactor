--TEST--
Stackful context switching on the Zend VM.
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn};

$as = new ActorSystem(true);

class A extends Actor
{
    public function __construct()
    {
        $this->send($this, 1);
    }

    public function receive($sender, $message)
    {
        var_dump($message); // int(1)
        $this->send('B', 2);
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
    public function receive($sender, $message)
    {
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

spawn('B', B::class);
spawn('A', A::class);
--EXPECT--
int(1)
int(2)
int(3)
int(4)
