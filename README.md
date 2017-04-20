A basic implementation of the actor model in PHP.

API:
```php
class ActorSystem
{
    public function block();
    public function shutdown();
}

abstract class Actor
{
    public abstract function receive($message);
    public final function receiveBlock(); // name as just receive?
    public final function remove();
    public final function send(Actor|string $to, mixed $message);
    public final function sendAfter($to, $message, $time); // todo
}
```
