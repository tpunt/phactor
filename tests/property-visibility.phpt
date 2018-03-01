--TEST--
Property visibility
--DESCRIPTION--
Ensure that visibility of properties for Actor-based objects is still enforced.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public $a = 1;
    protected $b = 2;
    private $c = 3;
    private $d = 4;

    public function receive()
    {
        $this->receiveBlock();
        var_dump($this->a, $this->b, $this->c);
        ActorSystem::shutdown();
    }
}

class Test2 extends Test
{
    private $d = 4;

    public function __construct()
    {
        var_dump($this->a, $this->b, @$this->c, $this->d);
        $this->send('test', null);
    }
}

new ActorRef(Test::class, [], 'test');
new ActorRef(Test2::class);
--EXPECT--
int(1)
int(2)
NULL
int(4)
int(1)
int(2)
int(3)
