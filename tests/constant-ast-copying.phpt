--TEST--
Ensure that the IS_CONSTANT_AST op array literal is properly copied.
--DESCRIPTION--
This requires a duplication of any zvals in the AST, as well as a new copy of
the zval ast value.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

const Z = 1;

$actorSystem = new ActorSystem();

class A
{
    public static function B($a = Z, $b = Z . Z)
    {
        var_dump($a, $b);
    }
}

class Test extends Actor
{
    public function receive()
    {
        A::B();
        ActorSystem::shutdown();
    }
}

new ActorRef(Test::class, [], 'test');
--EXPECT--
int(1)
string(2) "11"
