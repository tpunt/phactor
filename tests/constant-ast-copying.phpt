--TEST--
Ensure that the IS_CONSTANT_AST op array literal is properly copied.
--DESCRIPTION--
This requires a duplication of any zvals in the AST, as well as a new copy of
the zval ast value.
--FILE--
<?php

const Z = 1;

$actorSystem = new ActorSystem(true);

class A
{
    public static function B($a = Z, $b = Z . Z)
    {
        var_dump($a, $b);
    }
}

class Test extends Actor
{
    public function __construct()
    {
        $this->send($this, 1);
    }

    public function receive($sender, $message)
    {
        A::B();
        ActorSystem::shutdown();
    }
}

register('test', Test::class);

$actorSystem->block();
--EXPECT--
int(1)
string(2) "11"
