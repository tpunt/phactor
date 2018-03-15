--TEST--
Test the restart limiting of an actor (for its __construct() method)
--DESCRIPTION--
If an actor fails to invoke its __construct() method 5 times in a row then
escalate things to its supervisor.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef, Supervisor};

$as = new ActorSystem(1);

class A extends Actor
{
    public function receive(){$this->receiveBlock();}
}

class B extends Actor
{
    public function __construct(){var_dump('Creating B');}
    public function receive(){$this->receiveBlock();}
}

class C extends Actor
{
    private static $i = 0;

    public function __construct()
    {
        var_dump('Creating C');
        if (++self::$i < 6) {
            throw new Exception();
        }
    }

    public function receive(){ActorSystem::shutdown();}
}

$a = new ActorRef(A::class, [], 'a');
$s = new Supervisor($a, Supervisor::ONE_FOR_ONE);
$b = $s->newWorker(B::class, [], 'b');
$s2 = new Supervisor($b, Supervisor::ONE_FOR_ONE);
$c = $s2->newWorker(C::class, [], 'c');
--EXPECT--
string(10) "Creating B"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating B"
string(10) "Creating C"
