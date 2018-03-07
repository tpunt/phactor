--TEST--
Test the Supervisor::addWorker() method
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
    public function receive(){$this->receiveBlock();}
}

class C extends Actor
{
    public function receive(){$this->receiveBlock();}
}

class D extends Actor
{
    public function receive(){ActorSystem::shutdown();}
}

$a = new ActorRef(A::class);
$b = new ActorRef(B::class);
$c = new ActorRef(C::class);

$s = new Supervisor($a, Supervisor::ONE_FOR_ONE);
$s->addWorker($b);

try {
    new Supervisor(1);
} catch (Exception $e) {
    var_dump($e->getMessage());
}

try {
    new Supervisor($a, -1);
} catch (Exception $e) {
    var_dump($e->getMessage());
}

try {
    $s->addWorker($a);
} catch (Exception $e) {
    var_dump($e->getMessage());
}

try {
    new Supervisor($a, Supervisor::ONE_FOR_ONE);
} catch (Exception $e) {
    var_dump($e->getMessage());
}

$s3 = new Supervisor($c, Supervisor::ONE_FOR_ONE);

try {
    $s3->addWorker($b);
} catch (Exception $e) {
    var_dump($e->getMessage());
}

$s2 = new Supervisor($b, Supervisor::ONE_FOR_ONE);
$s2->addWorker($c);

try {
    $s3->addWorker($a);
} catch (Exception $e) {
    var_dump($e->getMessage());
}

new ActorRef(D::class);
--EXPECT--
string(30) "Invalid supervisor actor given"
string(34) "Invalid supervision strategy given"
string(49) "A cycle has been detected in the supervision tree"
string(34) "This actor is already a supervisor"
string(38) "This actor is already being supervised"
string(49) "A cycle has been detected in the supervision tree"
