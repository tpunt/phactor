--TEST--
Stackless context switching on the Zend VM.
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn};

$actorSystem = new ActorSystem(true);

spawn('test', Test::class);
spawn('test2', Test2::class);

class Test extends Actor
{
    public function receive($sender, $message)
    {
        var_dump($message);
        $this->send($sender, 2);
        var_dump($this->receiveBlock());
        var_dump($message);
        $this->send($sender, 4);
    }
}

class Test2 extends Actor
{
    private $workers;

    public function __construct()
    {
        $this->send('test', 1);
    }

    public function receive($sender, $message)
    {
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
