# Knihovna COCLS - implementace generátorů a asynchroních operací pomocí korutin

Knihovna COCLS poskytuje stavební třídy a funkce pro podporu generátorů, asynchornních operací, plánování a synchronizaci korutin.

**POZNÁMKA:** Všechny symboly v této dokumentaci jsou uváděny bez namespace.

```
using namespace cocls;
```

## Typ korutiny pro asynchroní operace

```
#include <cocls/async.h>
```
Korutina `async` představuje korutinu, která může být přerušena pomocí operátoru `co_await`. K vracení hodnoty pak použije `co_return`

```
async<T> coroutine(Args ... args) {
   //
   co_await ...;
   //
   co_await ...;
   //
   co_return ...;
}
```

**POZOR!** - korutina se zavoláním sama nespustí. Je potřeba ji spustit buď přímo, nebo pomocí dalších tříd - viz dále.

Zavoláním korutiny získáme objekt `async<T>`. Tento objekt lze přesouvat, ale nelze jej kopírovat. Zůstává platný dokud není korutina spuštěna - spuštěním korutiny je ekvivalentní operace jako přesun instance korutiny do interní částy knihovny.

### Spouštění korutiny - detached režim

Korutinu `async<T>` lze spustit v detached režimu. V tomto režimu se korutina spustí aniž by bylo možné získat informaci o ukončení běhu, ani výsledek korutiny (detached)

```
void async<T>::detach()
```


```
async<int> coro = coroutine(... args...);
coro.detach(); //spuštění v detached režimu
```

Jakmile je korutina spuštěna, proměnná `coro` může být zničena. Zničení nespuštěné korutiny je povolená operace s tím, že dojde k vyvolání destruktorů uložených parametrů

### Spuštění korutiny použitím operátoru co_await

Pokud připravenou korutinu použijeme jako výraz operátoru `co_await`, dojde ke spuštění korutiny za současného pozastavení aktuální korutiny. Jakmile korutina dokončí běh, je obnovena aktuální spící korutina a je vrácen výsledek volané korutiny,

```
int result = co_awat coroutine(... args....);
```

### Spuštění korutiny synchroně

V normálním vlákně lze spustit korutinu a vyčkat na výsledek pomocí funkce `.join()`. Operace je blokující volání, aktuální vlákno je zablokováno dokud korutina nevrátí výsledek

```
int result = coroutine(... args....).join();
```


### Spuštění korutiny - nastavení promisy výsledkem

Korutinu `async<T>` lze svázat s existujícím objektem `promise<T>` (viz dále), pak se korutina spustí a po dokončení se výsledkem vyplní svázana promise.

```
void async<T>::start(promise<T> &p);
```

```
promise<int> p =...;
async<int> coro = coroutine(... args...);
coro.start(p);
```

V příkladu nahoře po této operace budou obě proměnné, jako `coro` tak `p` prázdné a mohou být zničeny. Korutina nyní poběží nezávisle na kodu a jakmile dokončí, nastaví připojenou promisu.

### Spuštění korutiny - vrácení future<T>

Korutinu `async<T>` lze spustit tak, že výsledkem je objekt `future<T>` (viz dále). Tento objekt lze použít k čekání na výsledek korutiny.

```
future<T> async<T>::start();
```

```
async<int> coro = coroutine(... args...);
future<int> f = coro.start();
//...
int result = co_await f;
```

Pokud má být korutina spuštěna synchroně (s čekáním na dokončení) v rámci běžného kódu (ne v korutině), lze použít f.wait();

```
async<int> coro = coroutine(... args...);
future<int> f = coro.start();
//...
int result = f.wait();
```

## Třídy future<T> a promise<T>

```
#include <cocls/future.h>
```

Tyto třídy jsou **základním synchronizačním nástrojem** pro komunikaci mezi korutinami a zbytkem kódu.

* **future<T>** - představuje budoucí proměnnou daného typu. Hodnota je nastavena později. Na okamžik nastavení hodnoty lze čekat asynchronně pomocí `co_await` nebo synchroně pomocí metody `wait()` případně `sync()`. **POZOR!** Objekt **nelze ani kopírovat ani přesouvat**. Lze jej však vracet z funkce. Sdílený stav **není alokován na heapu**.

* **promise<T>** - představuje objekt svázaný s instancí future<T>. Slouží k nastavení hodnoty svázané `future<T>`. Objekt nelze kopírovat, ale lze jej přesouvat. Nastavit hodnotu lze pouze jednou, poté to již není možné, nastavením hodnoty dojde atomicky k rozpojení svazku. Tato operace je MT safe, více vláken se může pokoušet nastavit promise<T>, pouze první uspějě. Objekt má vlastnosti `callable`. Je tedy možné vložit objekt do `std::function`

### Konstrukce future<T> a promise

```
future<int> f([&](promise<int> promise) {
    do_something_with_promise(std::move(promise));
});
int result = co_await f;
```

### Konstrukce future<T> vrácené z funkce

Toto je nejčastější způsob konstrukce future<T> pokud má být vrácena a přitom nejde  o korutinu. Může se jednat o napojení future do systému založeným na callback funkcích.

```
future<int> retrieve_value() {
    return [&](auto promise) {
        listener.register_callbacak(std::move(promise));
        listener.request_value();
    };
}
```

Výše uvedený příklad předpokládá, že `listener` je nějaký asynchroně řízení naslouchač dat. Nejprve tedy zaregistrujeme promisu (může být zaregistrován jako callback) a následně požádáme listener o získání hodnoty. Tato operace může proběhnout asynchroně, tedy ihned po žádosti nemusí být hodnota k dispozici a kód pokračuje návratem z funkce. K nastavení hodnoty dojde k zavolámím `promise` s hodnotou

```
promise(<value>)
```

### Konstrukce future<T> korutinou.

Stačí do konstruktoru vložit `async<T>` a dojde ke spuštění korutiny, jejiž výsledek nastaví future

```
future<int> f(coroutine(...args...));
```

```
future<int> run_async(...args...) {
    return coroutine(...args...);
}
```

### Konstrukce návratové hodnoty future<T> korutinou přímo

Třída future<T> nabízí zkratku pro psaní korutin. Funkce která vrací `future<T>` může ve svém těle použít `co_await` a `co_retunr` aniž by si překladač stežoval, protože třída `future` referencuje typ async<T> pro systém korutin

```
future<int> coroutine_example() {
    co_await...; //

    co_return
}
```

Takto napsaná korutina vychází z `async<T>`, je automaticky spuštěna, pokud je zavolána a vrácena `future<T>` je nastavena výsledkem korutiny

Tato zkratka má určité drobné nevýhody. Zatímco objekt `async<T>` lze přesouvat, objekt `future<T>` nikoliv, Typ `async<T>` lze také použít s alokátorem (viz dále), samotné `future<T>` takto použít nejde. Pokud je tohle problém, lze výše zmíněnou zkratku přepsat na dvě funkce


```
future<int> coroutine_example(args) {
    return coroutine_example_coro(args);
}
async<int> coroutine_example_coro(args) {
    co_await...; //

    co_return...;
}
```

### Příjem výsledku future<T> do existující instance

Třída future<T> není přesouvatelná ani kopírovatelná a to proto, že adresa proměnné se nesmí změnit po dobu, kdy čeká na nastavení hodnoty. Nelze ani použít přiřazení protože tento operátor vyžaduje kopii nebo přesun. Třída definuje operátor <<, kterým lze převzít výsledek volání funkce, která vrací future<T>.

```
future<int> coroutine_example();


future<int> val;
val << [&]{return coroutine_example();};
int result = co_await val;
//lze opakovaně
val << [&]{return coroutine_example();};
int result = co_await val;
```

Operátor << vyžaduje na pravé straně funkci, která vrací future<T>. V rámci zpracování je funkce zavolána a výsledná future je zkonstruovaná v instanci na levé straně operátoru. Pokud je třeba zavolat funkci speciálním způsobem (metoda, parametry), lze volání zabalit do lambda funkce, nebo použít std::bind.

### Zahození výsledku future<T>

Třída future<T> je označena `[[nodiscard]]` záměrně, protože nesmí být zahozena před nastavením hodnoty. Zahození proměnné před nastavením hodnoty je považováno za chybu, tato chyba se oznámí pomocí `assert()`, a v release je to UB.

Pokud je přesto třeba výsledek zahodit, je třeba použít funkci  `cocls::discard()`. Tato funkce očekává funkci, která vrací  `future<T>` jenž má být zahozena

```
discard([&]{return coroutine_example();}
```

Funkce `discard()` alokuje future<T> na haldě a v okamžiku nastavení hodnoty ji zničí. Tuto funkci použijte, pokud nemáte jinou možnost jak zahodit výsledek volání. Pokud je volání
ve formě korutiny, pak je lepší použít async<T>::detach(), která též zahazuje výsledek korutiny a přitom nepotřebuje alokovat speciální místo na haldě.



### Objekt shared_future<T>

```
#include <cocls/shared_future.h>
```

Třída `shared_future<T>` je na rozdíl od `future<T>` objekt, který lze kopírovat nebo přesouvat. Těmito operacemi se sdílí vnitřní stav, který je alokován na heapu. Více korutin může čekat na tento typ future.

Tato future je sdílena podobně jako shared_ptr<T>. Pokud je objekt ve stavu `pending` (čeká na hodnotu), je přiznaná jedna reference navíc, což umožňuje sdílenou future zahodit, tedy zrušit všechny reference aniž by to způsobilo chybu nebo UB. Sdíleny stav bude existovat dokud se výsledek nenastaví.


Instanci `shared_future<T>` konstruujeme pomocí argumentů podobně jako `discard`. Do konstruktoru musíme vložit funkci, která vrací `future<T>`. Konstruktor si funkci zavolá a nabídne sdílené místo pro návratovou hodnotu.

```
shared_future<int> f([&]{return coroutine_example();});
int result = co_await f;


```

### Zavolání funkce/callbacku při nastavení future

Místo aktivního čekání na future, lze požadovat, aby se při nastavení future zavolal callback, a jemu se předala hodnota.

```
#include <cocls/callback_awaiterr.h>
```

Stejně jako ve výše uvedených situacích budeme instanci `future<T>` konstruovat pomocí
lambda funkce, která instanci vrací. K vytvoření callbacku použíjeme `callback_await`

```
callback_await<future<int> >([&](await_result<int> result){
        if (result) {
            process_result(*result);
        } else {
            handle_exception(std::current_exception());
        }
    },[&]{
        return a_function_returning_future_int();
    });
```

Funkce `callback_await` vyžaduje specifikovat typ awaitable nebo awaiteru v parametru šablony, zde `callback_await<future<int>` specifikuje, že chceme čekat na `future<int>`. Prvním parametrem funkce je samotný callback, který může být ve formě lambdy, nebo jiného invokable objektu. Tato funkce obdrží výsledek ve formě `await_result<T>`. Hodnotu lze získat přes funkci `get()`. Pokud awaitable nevrací výsledek (vrací void), je přesto nutné zavolat get(), která též nic nevrací, ale může být vyhozena výjimka. Je nutné dodat, že `await_result` funguje správně jen v kontextu aktuální funkce.

Funkce `callback_await` musí callback alokovat na haldě. Proto existuje varianta, která
přijímá alokátor. Allokátor je třeba specifikovat v šabloně

```
reusable_storage alloc;
callback_await<reusable_storage, future<int> >([&](alloc, await_result<int> result) {...
```

Pozor na platnost allokátoru, měl by existovat po celou dobu dokud není callback zavolán.


**TIP:** Funkce `callback_await` může být použita i pro jiné awaitable než pro future<T>.


### Dropnutí promise

Promise by měla být nakonec nastavena. Avšak je možné promisu zničit bez nastavení hodnoty. Tomuto stavu se říká "drop". Pokud je promisa dropnuta, není nikdo, kdo by čekající futuru nastavil. V takovém případě je do future nastaven stav "bez hodnoty". Při pokusu načíst tuto hodnotu se pak výhodí výjimka na straně čekající korutiny

Promise může být dropnuta i ručně zavoláním promise s parametrem `cocls::drop`. To je konstanta, která způsobí, že future bude nastavena bez hodnoty

```
    promise<int> p = ...;
    p(cocls::drop); //drop promise
```

### Nastavení výjimku

Promise nemusí být jen použita k nastavení hodnoty, ale k nahlášení výjimky. Pokud je promise zavolána s objektem `std::exception_ptr`, pak se uložená výjimka vyhodí na čekající straně.

```
    try {
        //operation
        p(calc());
    } catch (...) {
        p(std::current_exception());
    }
```




### Další operace s future<T>

* **synchronní čekání** - .`wait()` - blokne vlákno dokud se nenastaví výsledek, pak vrací výsledek
* **synchronizace bez vyzvednutí hodnoty** - `sync()` - Tato funkce pouze blokne vlákno, dokud se hodnota nenastaví. Ale na hodnotu nepřistupuje, takže nevyhazuje ani výjimku, pokud byla nastavena místo hodnoty
* **detekce stavy "bez hodnoty" - `has_value()` - tato funkce testuje, zda future byla nastavena hodnotou nebo výjimkou. Pak vrací `true`. Pokud byla nastavena bez hodnoty, vrátí `false`. Pokud future ještě nastavena nebyla, pak udělá sync(). Tuto funkci lze zavolat i přes `co_await` ve formě (`co_await f.has_value()`), pak se provede čekání s uspáním korutiny a funkce vrací true/false podle výsledku - přičemž nevyhazuje výjimku.



## Řízení běhu korutin

### Dva stavy vlákna

V rámci korutiny async<T> existuje evidence dvou stavů vlákna
* **normální běh (normal mode)** - ve kterém jsou voláný funkce, ne však korutiny
* **běh v korutině (coro mode)** - v tomto stavu je v zásobníku aktivní aspoň jeden rámec patřící korutině.

**Poznámka** - režim je evidován jen v rámci objektů z knihovny `cocls`. Pokud dojde k aktivaci rámce patřící jiné knihovně, nemusí detekce tohoto rámce fungovat. Kdykoliv je spuštěna, nebo obnovena korutina v *normal mode*, přejde vlákno do stavu *coro mode* a tento stav opustí v okamžiku, kdy se tato korutina ukončí nebo přeruší.

### Řízení korutin v *coro mode*

Pokud jakákoliv korutina má být obnovena v režímu *coro mode*, nedojde k jejímu okamžitému obnovení. Místo toho je korutina zařazena do fronty k obnovení. Tato fronta existuje v rámci vlákna (každé vlákno má oddělenou frontu). Ukončení nebo přerušení aktuálně běžící korutiny obnoví další korutinu z fronty. Režim *coro mode* se ukončí, jakmile je fronta prázdná

Pokud Váš kód řídí korutiny manuálně a chcete využít tuto vlastnost, nevolejte `h.resume()`, místo toho zavolejte `coro_queue::resume(h)`. Tuto funkci lze bezpečně volat i v *normal mode*, pak se vlákno přepne do *coro mode* podle výše uvedených pravidel.

**Poznámka** - odlišení *coro mode* od *normal mode* řeší situace, ve kterých by mohlo dojít k naskládání rámců korutin na aktuálním zásobníku. Navíc pokud by došlo k pokusu obnovení korutiny, která již má svůj rámec aktivní, je toto považováno za chybu a výsledkem je UB. Obnovování korutin v režimu *coro mode* znemožňuje naskládání rámci na sebe

**Výjimka** - některé objekty vyžadují použití volání přímého `h.resume()`, pak k částečnému naskládání dojít může. Typicky pokud se používá synchroní přístup v rámci korutiny, například synchronní generátor, nebo `signal::collector`

### Využití symmetrického transferu

Použití fronty umožňuje v některých situacích využít symmetrický transfer. To znamená, že když jedna korutina je uspána, může jiná korutina, připravná ve frontě, být obnovena v rámci jedné operace. Překladače často umí takto rychle přepínat mezi korutinami.

### Funkce pause()

Funkce pause() uspí aktuální korutinu a obnoví první korutinu připravenou ve frontě. Současná korutina je zároveň vložena do fronty aby byla obnovena příště

```
co_await pause();
```

Pokud v aktuálním vlákně není připravena žádná korutina k běhu, pak funkce nedělá nic. Již řazené korutiny se obnovuji v pořadí zařazení. Pokud víc korutin opakovaně používá tuto funkci, pak se exekuce střídá v pořadí jejich prvotního zařazení (fronta)

### Přepnutí do korutiny pomocí `suspend_point`

Třída `suspend_point` je návratovou hodnotou některých funkcí. Jedná se o šablonu, která si nese v prvním parametru návratovou hodnotu a v druhém parametru pak většinou označení toho, kdo třídu implementuje. Každý vlastník může implementovat jinak dle potřeby 

```
suspsend_point<bool, future<int> > result = promise(42);
```

Funkce nebo metody které vrací `suspend_point` označují místo, na kterém by bylo vhodné provést `co_await`. Výsledkem operace je pak hodnota typu, který je v prvním parametru.

```
bool val = co_await result;
```

Rozdíl od `future<T>` nebo jiných awaiterů, použití `co_await` zde není povinné, a  k výsledku lze přistoupit přímo

```
bool val = result
```

nebo ve zkratce

```
bool val = promise(42)
```

Použití `co_await` však dává korutině možnost předat řízení korutině, která se provedenou operací se stala připravená ke spuštění. Například, pokud dojde k nastavení promise (zde `p`) na nějakou hodnotu, svázaná korutina čekající na tuto hodnotu je ihned připravená ke spuštění. V rámci *coro_mode* by se však zařadila do fronty připravených korutin a spustila by se až teprve až se současná korutina uspí. Navíc pokud je ve frontě více korutin, bude se postupovat popořadě. To se stane v případě, že `suspend_point` ignorujeme a vyzvedneme si výsledek přímo, nebo jej plně zahodíme. Použitím `co_await` na určeném `suspend_point` se současná korutina uspí a korutina připravená k běhu se probudí a může ihned reagovat na nový stav.

Výsledek ve formě `suspend_point` nabídí 
* `promise` při nastavování hodnoty, přičemž hodnotou je `bool`. **True** znamená, že korutina se spustila, **false** znamená, že promise již není svázána (pak k uspání nedošlo). Pokud `co_await`ujeme tento `suspend_point` pak se probudí korutina, která čekala na nastavenou hodnotu.
* `mutex` a to ve funkci `release()` u pod-objektu `ownership`. To umožňuje uvolnit vlastnictví a ihned přepnout do korutiny, která je novým vlastníkem zámku. 


## Awaiter

```
#include <cocls/awaiter.h>
```

Awaiter je objekt, který lze použít s operátorem `co_await`. Může být přímo i awaitable - tedy objekt, který podporuje `co_await` operaci - nebo může být vytvořen awaitable objektem před zahájením čekání.

Awaiter často vzniká jako důsledek zavolání **operator co_await** na awaitable objektu nebo funkci

V knihovně `cocls` je základovou třídu pro většinu awaiterů třída `awaiter`. Přes tuto base třídu lze přistupovat k awaiteru, na kterém byla korutina uspána. Obsahuje dvě funkce

* **resume()** - probudí spící korutinu
* **resume_handle()** - vrátí handle spící korutiny, má se za to, že účelem volání je korutinu probudit skrze handle.

Awaitery se často registrují na kolektorech tak aby třída, která má schopnost uspávat a probouzet korutiny, dokázala snadno identifikovat a probudit korutinu, kterou probudit chce.

Využívá se faktu, že během toho, co korutina je uspaná, tak její awaiter, na kterém spí, nikam neodejde, jeho pointer bude platný po celou dobu uspání. A jakmile je korutina probuzena, není třeba více přistupovat na objet awaiter.

Pro správu většího množství awaiterů se zakládá kolektor, který je reprezentován jako `std::atomic<awaiter *> _awt_collector`. Awaiter dále nabízí funkci **subscribe(collector)** kterým lze MT-safe přidat awaitera do kolektoru. Na druhé strane pak metodu **resume_chain**, kterou lze atomicky probudit všechny korutiny čekající v daném kolektoru. Někdy je potřeba kolektor i deaktivovat, aby nepřijímal další registrace. K tomu slouží **subscribe_check_disabled()**, která provede registraci jen když není kolektor deaktivovaný. K tomu existuje opačná operace **resume_chain_set_ready**, tato operace atomicky obnoví všechny korutiny a zároveň deaktivuje kolektor. Jako deaktivující hodnota se používá globální instance `&awaiter::disabled`;

### Použití awaiteru k notifikaci o dokončení operace

Awaiter však nemusí jen budit korutinu. Awaiter nabízí funkcí `set_resume_fn`, kterou lze nastavit funkci, která se zavolá v situaci, kdy by měla bý korutina zbuzena. Tímto lze relizovat například `callback_awaiter` - zavolá callback.

Rozhraní `set_resume_fn` očekává C-like statickou funkci - lze použít lambdu bez clousure. Funkce má následující prototip

```
static void resume_fn(awaiter *_this, void *_context, std::coroutine_handle<> &_out_handle) noexcept;
```

* **_this** - pointer na `awaiter` jehož instance byla oslovena. Pokud dědíme awaitera, musíme si pointer `static_cast<>`
* **_context** - libovolný ukazatel na cokoliv, nastavují se funkcí `set_resume_fn`
* **_out_handle** - reference na handle nějaké korutiny, je považován za výstupní proměnnou. Funkce jej může ignorovat pokud jej nepoužívá. Pokud však výsledkem zpracování je korutina, která má být probuzena, je vhodní nastavit její handle do této proměnné před návratem z funkce. Je to efektivnější, než ve funkci volat přímo `handle.resume()`

Volba **statické funkce bez kontextu** byla zvolena za účelem udržení objektu awaiter v jednoduchém layout bez nutnosti alokovat paměť například pro clousure dané funkce. Ten kdo si awaitery dědí má z pravidla přístup k dalším částem svého objektu zkrze *_this*. Bylo také záměrně upuštěno od použití virtuálních funkcí, protože většina awaiterů `resume_fn` nepoyužívá a je orientováno čistě na korutiny, kde se funkce nevolá a tím se redukuje množství indirekce.

* `co_awaiter<promise_type>` - zajišťuje operaci co_await na většině awaitable objektů
* `sync_awaiter` - umožňuje uspávat a probouze celá vlákna (`sync_awaiter::wait_sync`)
* `switch_to_awt` - implementuje přepnutí kontextu pro funkcí `switch_to`
* `self` - neuspí korutinu, ale vrací její handle `auto myhandle = co_await self()`
* `thread_pool::co_awaiter` - přestěhuje korutinu do jiného vlákna




## Generátor

```
#include <cocls/generator>
```

Generátor je třída, která tvoří základ korutiny, ve které lze používat `co_yield`.

```
generator<T, Arg> gen_coroutine(...args...) {
    co_yield...;
    co_yield...;
    co_return;
}
```

* **T** - typ hodnoty, který generátor posíla zkrze `co_yield` ven z generátoru (generátor pak tento typ vrací při každém zavolání) - nesmí být `void`
* **Arg** - typ hodnoty, která vstupuje do generátoru při volání generátoru, a vystupuje jako výsledek operace `co_yield`. Může být `void` (výchozí), pak generátor nevyžaduje žádný argument při každé iteraci

### Vlasnosti objektu

* **nelze kopírovat** - objekt generátoru nelze kopírovat
* **lze přesouvat** - objekt generátoru lze přesouvat
* **lze předčaně destruovat** - pokud generátor zrovna nepracuje, čeká na další zavolání, lze generátor zničit, aniž by to způsobilo problémy a to i v případě, že generátor byl přerušen uprostřed vypočtu (musí čekat na `co_yield`). Zajistěte si správné volání destruktorů, všechny objekty musí splňovat RAII. Samotný akt destrukce lze zachytit pouze destruktorem.


### Korutina

Korutina generátoru vždy startuje v zastaveném stavu a je probuzena při prvním zavolání generátoru. Poté je přerušena na co_yield a při dalším zavolání opět probuzena a pokračuje v činnosti za co_yield

Korutina může používat co_await (viz dále)

### Různé typy generátorů

* **synchronní** - mezi co_yield nedochází k jinému přerušení (žádný co_await)
* **asynchronní** - mezi co_yield používá generátor co_await

### Způsoby volání generátoru

* **synchornní** - volající počká, dokud není vygenerovaný výsledek. Pokud se jedná o asynchornní generátor, zahrnuje to blokující operaci
* **asynchronní** - volání generátoru vyžaduje použití `co_await`. V takovém případě je volající korutina uspána a teprve až generátor vygeneruje hodnoty, je korutina probuzena. V tomto režimu nezáleží na tom, jestli je genrátor synchronní nebo asynchronní.
* **iterátorem** - umožňuje používat standardní iterátory pro iteraci výsledků. Generátor lze použít v range_for. Tento přístup je implicitně synchornní
* **vrací future** - tímto způsobem se zavoláním generátoru vrátí future, na kterou je třeba počkat

### Počet cyklů

* **omezený** - generátor má omezený počet cyklů a pak skončí. Volající musí být schopen detekovat, že byla vygenerovaná poslední hodnota
* **nekonečný** - generátor má schopnost generovat nekonečně hodnot. Generátor lze ukončit pouze zničením instance.

### API - next() a value()

#### Synchronní přístup

```
generator<int> gen = ....;
while (gen.next()) {
    print(gen.value());
}
```

S každým dalším zavoláním `.next()` se vygeneruje hodnota. Funkce vrací `true` pokud hodnota
byla vygenerovaná, nebo false, pokud generátor skončil

Hodnota je dostupná přes `.value()`. Je vrácena jako reference. Tato reference je platná do dalšího `.next()`

#### Asynchronní přístup

```
generator<int> gen = ....;
while (co_await gen.next()) {
    print(gen.value());
}
```

### API - iterátory
```
generator<int> gen = ....;
auto iter = gen.begin();
while (iter != gen.end()) {
    print(*iter);
    ++iter;
}
```

Ranged_for:

```
generator<int> gen = ....;
for(auto &val: gen) {
    print(val);
}
```

### API - future

V tomto případě generátor voláme jako funkci

```
generator<int> gen = ....;
while(true) {
    future<int> val = gen();
    if (val.has_value()) {
        print(*val);
    } else {
        break;
    }
}
```

S použitím operátoru <<


```
generator<int> gen = ....;
future<int> val = gen();
while(val.has_value()) {
    print(*val);
    val << gen;
}
```

Asynchronní přístup

```
generator<int> gen = ....;
future<int> val = gen();
while(co_await val.has_value()) {
    print(*val);
    val << gen;
}
```

### Generátor s argumentem

Pokud je **Arg** jiný než **void**, pak tento argument se nastavuje při volání funkce `next()`

```
generator<int, int> gen = ....;
int counter = 1;
while (gen.next(counter)) {
    print(gen.value());
    ++counter;
}
```

V rámci korutiny je pak hodnota k dispozici přes

```
int val = co_yield <expr>;
```

#### Přístup k první hodnotě argumentu.

Při prvním zavolání generátoru s argumentem není zaslaná hodnota přímo k dispozici. Při zavolání `co_yield` se očekává již nějaký výsledek a pak by se načetla druhá zaslaná hodnota argumentu. Proto lze v tomto případě použít zápis `co_yield nullptr`;

```
generator<int,int> doubler() {
    //retrieve very first argument
    int arg = co_yield nullptr;
    while (true) {
        arg = co_yield arg * 2; //double every argument and return it as result
    }
}

void print_gen() {
    auto gen = doubler();
    for (int i = 0; i < 10; i++) {
        auto v = doubler(i);
        print(*v); //*v is shortcut to v.wait();
    }
}
```

### Generátor aggregator

Víc generátorů lze agregovat do jednoho generátoru. Pokud se jedná o synchroní generátory, pak agregace způsobí, že jednotlivé generátory se budou střídat v generování další hodnoty. Pokud se jedná o asynchroní generátory, pak agregovaný generátor vždy vygeneruje hodnotu
toho generátoru, který jako první dokončil svůj generační proces. Další generátory se řadí do fronty,

```
generator<T,Arg> generator_aggregator(std::vector<generator<T,Arg> gens);
```

Pozor na to, že generátory nejsou kopírovatelné. Vektor připravených generátorů je tedy nutné poslat přes std::move()

Výsledkem operace je jeden generátor, který lze použít standardním způsobem. Generátor je synchronní, pokud všechny generátory v agregaci jsou synchroní. Jeden asynchroní generátor způsobí, že generátor je asynchorní.


Pokud se agregují generátory s argumentem, tak první zavolání agregátoru způsobí inicializaci všech generátorů v agregaci stejným argumentem. Každé další zavolání způsobí, že argument si převezme generátor, jehož hodnota byla vrácena předchozím voláním.



## Zámek (mutex)

Korutiny mají limitované možnosti používat zámky. Není možné držet zámek (`std::mutex` a varianty) mezi co_await, pokud hrozí, že bude exekuce přestěhovaná do jiného vlákna. Navíc držení zámku počas uspání není bezpečné, může dojít k deadlocku.

Aby bylo možné v rámci korutin zámky používat tak, aby zámek neblokoval vlákno, pouze čekající korutinu, existuje třída `mutex` v rámci knihovny `cocls`

```
cocls::mutex mx;

async<void> do_something() {
    auto ownership = co_await mx.lock();
    //nyní vlastníme mutex mx
    co_await ...;
    co_await ...;
    co_return;
    //zámek se uvolní zničením objektu ownership
}
```

Narozdíl od standardního mutexu, zde se funkce `lock()` musí volat s `co_await`. Pokud je zámek vlastněn jiným vlastníkem, pak se korutina uspí a je probuzena jakmile se vlastnictví
mutexu uvolní. Vlastnictví mutexu se sleduje objektem `ownership`. Tento objekt není kopírovatelný, pouze přesouvatelný. Jakmile je objekt opuštěn, nebo je zavolána funkce `.release()`, pak je zámek odemčen.

Objekt `mutex` implementuje frontu čekajících korutin. Pokud některá korutina uvolní mutex,
vlastnictví se automaticky přenese na první čekající korutinu. Vlastní obnovení korutiny se řídí podle pravidle vlákna ve stavu *coro mode*. Tedy obnovená korutina je umístěna do fronty k obnovení a je obnovena podle plánu jakmile se aktuální korutina dokončí nebo přeruší. Stále běžící korutina může tento proces urychlit použítím funkce `pause()`

Uvolnění mutexu předčasně


```
cocls::mutex mx;

async<void> do_something() {
    auto ownership = co_await mx.lock();
    //nyní vlastníme mutex mx
    co_await ...;
    ownership.release();
    //zámek se uvolní zničením objektu ownership
    //nový vlastník ale není obnoven
    long_op();
    co_return;
    //až tady se ke slovu dostane nový vlastník mutext
}
```

Urychlení obnovení čekající korutiny

```
cocls::mutex mx;

async<void> do_something() {
    auto ownership = co_await mx.lock();
    //nyní vlastníme mutex mx
    co_await ...;
    ownership.release();
    //zámek se uvolní zničením objektu ownership
    //uvolni vlákno novému vlastníkovi
    co_await pause();
    long_op();
    co_return;
}
```
### Další operace mutexu

* **mutex::try_lock()** - lze volat i v *normal mode*, neblokuje
* **mutex::lock().wait()** - zamkne mutex v *normal mode*, blokující volání


## Fronta (queue)

```
#include <cocls/queue.h>
```

Fronta existuje ve dvou variantách `queue<T>` a `limited_queue<T>`

Fronta umožňuje nechat korutiny reagovat na hodnoty vkládané do fronty

* Pokud je fronta prázdná, pak operace pop() způsobí, že aktuální korutina je uspaná a je následně probuze první vloženou hodnotou
* Pokud fronta není prázdné, pak operace pop() korutinu neuspí a je ihned vybrána první hodnota z fronty
* Pokud je fronta limited_queue plná, pak korutina volající operaci  push() je uspána a je následně probuzena pokud někdo vyzvedne první hodnotu z fronty a uvolní v ní místo.
* Na hodnoty ve froně může čekat víc korutin současně (multiple consumers).
* Pushovat hodnot může vícero producerů, operace je MT bezpečná. (multiple producers)
* V případě limited_queue<T> může vícero producerů čekat na uvolnění místa ve frontě při push()

**Poznámka** - funkce pop() a funkce push() (u limited-queue) vrací future<T>.

```
async<void> read_queue(queue<int> &q) {
    while(true) {
        int val = co_await q.pop();
        print(val);
    }
}

void fill_queue(queue<int> &q) {
    q.push(10);
}
```

### Odblokování čekajícího.

Pokud korutina čeká na funkci .pop(), lze ji odblokovat a zaslat ji výjimku.

```
void stop_reader(queue<int> &q) {
    q.unblock_pop(std::make_exception_ptr(await_canceled_exception()));;
}
```
**Pozor:** Pokud ve frontě nikdo nečeká, nebo je fronta plná neodebraných hodnot, k odblokaci
nedojde

Typické použití je k implementaci timeoutu. Koroutina, která čte si nainstaluje timer, který zavolá `unblock_pop`, pokud vyprší čas čekání na hodnotu ve frontě. Pakliže je hodnota získána, může být timer odinstalován.

## Signal

Signal je objekt, který propojuje dvě části kódu, kdy jedna generuje signály v podobě hodnot (signal generator) a druhá je těm signálům naslouchá a reaguje na ně. Je to podobný pattern jako producer a consumer.

Rozdíl mezi frontou a signálem je v těsnější vazbě mezi vyprodukování hodnoty a její konzumace. Zatímco u fronty může producent generovat nové hodnoty nezávisle na tom, zda je konzumenti stíhají konzumovat, v tomto případě se provádí synchronizace, kdy producent je zastaven do doby, než je produkovaná hodnota z konzumována.

Dalším rozdílem je, že může existovat více konzumentů a všichni obdrží stejnou hodnotu.

Objekt **signal** má dvě strany reprezentované dvěmi podřídami

* **signal::collector** - jedná se o **callable** object. Přijímá argumenty, které se použijí ke konstrukci předané hodnoty. Pokud se hodnota předává jako rvalue reference nebo lvalue reference, pak se interně předává pouze reference a nedochází ke kopii nebo konstrukci hodnoty.

* **signal::emitter** - jedná se o awaitera, na kterého lze čekat pomocí `co_await`. Korutina se uspí a jakmile se objeví signál, je vzbuzena a získá referenci na připojenou hodnotu.

Korutina musí opakovat `co_await` čekání na awaiteru, aby mohla získat další hodnotu. Je přitom nutné, aby korutina nebyla uspána z jiného důvodu než čekání další hodnotu. Pokud se takl stane, může generátor signálu vygenerovat novou hodnotu, která nebude korutinou zachycena (miss), protože na ní nebude čekat.

Vlákno během volání kolektoru je blokováno bo dobu vyvolání všech čekajících korutin. Jakmile je však korutina uspána, vlákno je odblokováno (veškeré čekající korutiny se volají v tomto vlákně). Pokud se tedy korutina přesune do jiného vlákna, odblokuje vlákno kolektoru a proto může kolektor získat novou hodnotu mezitím co korutina čeká na dokončení jiné asynchroní operace.


```
signal<int> sig;

async<void> consumer(signal<int> &sig) {
    try {
        auto e = sig.get_emitter();
        while(true) {
            int val = co_await e;
            print(val);
        }
    } catch (const await_canceled_exception &) {
        //done
    }
}

void generate(signal<int> &sig) {
    auto c = sig.get_collector();
    for (int i = 0; i < 10; i++) {
        c(i);
    }
}

```
### Registrace kolektoru až v korutině.

Někdy je potřeba aby korutina sama registrovala kolektor na signal generátoru a přitom už byla připravená přijmout první hodnotu. K tomu slouží **signal<>::hook_up**. Tato funkce přijímá funkci, ve které se očekává kolektor. Funkce má provést registraci kolektoru. Zároveň funkce vrací emitter, na který lze čekat. Registrace se provede při prvním čekání

```
async<void> consumer(signal_producer<int> &prod) {
    try {
        auto e = signal<int>::hook_up([&](auto collector) {
                prod.subscribe(std::move(collector));
            });
        while(true) {
            int val = co_await e;
            print(val);
        }
    } catch (const await_canceled_exception &) {
        //done
    }
}

```
## Thread pool a plánovač

```
#include <cocls/thread_pool.h>
#include <cocls/scheduler.h>
```

### Thread pool

Objekt `thread_pool` představuje kolekci běžících vláken. Jejich počet se určuje v konstruktoru.

Korutina může alokovat vlákno v thread_poolu tak, že jednoduše použije `co_await` na instanci poolu

```
async<void> threaded(thread_pool &pool)  {
    co_await pool;
    //running in thread pool
}
```

Každé vlákno automatick běží v *coro mode*. Každé vlákno tak má k dispozici frontu lokálně připravených korutin.

Kromě toho, API thread_poolu nabízí následující metody

* **run(...)** - spustí funkci nebo async<> korutinu ve vlákně. Vrací future<T> výsledku (i pro void)
* **run_detached(...)** - spustí funkci nebo async<> korutinu ve vlákně. Ignoruje výsledek nebo i případnou výjimku
* **resolve(p,args...)** - nastaví promisu **p** hodnotou konstruovanou pomocí parametru **args**, tuto činnost provede ve vlákně, takže případná korutina čekající na výsledek je v tomtéž vlákně obnovena
* **any_enqueued()** - vrací **true**, pokud současný běh kódu blokuje nebo může blokovat čekající úlohy nebo korutiny. Je to dobré testovat, pokud by kód chtěl vlákno blokovat. Pakliže je vráceno **true**, pak by se kód měl vyvarovat blokujících operací

Pokud korutina zavolá `co_await pool` ve vlálně, které patří tomu poolu, je to ekvivalentní funkci `co_await pause()` s tím, že korutina může alokovat jiné vlákno a v uvolněném vlákně mohou běžet čekající úlohy.

Samotné `co_await pause()` lze použít, ale pouze přenechá současné vlákno čekajícím korutinám na stejném vlákně, a pak pokračuje ve stejném vlákně



### Plánovač (scheduler)

Plánovač scheduler zajišťuje zejména časové plánování korutin. Plánovač se buď konstruuje jako úloha běžící v thread_poolu, nebo samostatně. Pokud běží samostatně, povětšinu času blokuje aktuální vlákno na němž provádí plánování korutin, které plánovač používají. Pokud běží v thread_poolu, blokuje jedno vlákno.

```
scheduler sch1;
thread_pool pool(10);
scheduler sch2(pool)
```

Plánovač lze také spustit v samostatném vlákně - dvěma způsoby

```
scheduler sch3;
sch3.start_thread();
```

```
std::thread sch_thr;
scheduler sch4(thr)
```

Plánovač lze v korutině použít pomocí funkcí **sleep_for** a **sleep_until**. Tyto funkce lze volat přes `co_await`, protože výsledkem volání `future<void>`.

Součástí volání těchto dvou funkcí je i identifikátor typu `void *`. Tímto identifikátorem lze později naplánovanou operaci zrušit s tím, že patřičná future<void> vyhodí výjimku (kterou lze nastavit)

### Spuštění plánovače v single-thread mode.

Plánovač lze spustit v single-thread mode pomocí funkce **start(Awt)**. Jako parametrem uvedem libovolného awaitera nebo awaitable (například future<T>, stačí jen referenci). Plánovač bude provádět svou plánovací činnost dokud awaiter nebude awaiter aktivován - tedy například u future, dokud nebude hodnota nastavena. Pak funkce vrátí výsledek operace

Funguje to tedy stejným způsobem jako `co_await Awt` s tím, že se používá v *normal mode* a během čekání na výsledek se provádí plánování. Jakmile je výsledek k dispozici, plánování se přeruší - ale registrované úlohy se nesmažou, čili pokud je plánovač znovu spuštěn,  plánovací činnost pokračuje

### Generátor intervalů

Funkce **interval** představuje generátor intervalu. Parametrem se zadává interval. Pokud je generátor zavolán, vrátí future, která se nastaví po zadaném intervalu. Další interval se počítá od času posledního intervalu nezávisle na tom, kolik času uplynulo do dalšího zavolání, za předpokladu, že ten čas nebyl delší než samotný interval.


## Korutiny alokované pomocí alokátorů

Běžné korutiny se alokují na heapu. Knihovna cocls však nabízí i možnost alokovat korutinu prostřednictvím alokátoru. Je to určeno pro zkušenější programátory, zato lze dosáhnout vyšší efektivitu při volání korutin.

### Korutina podporující alokaci alokátorem

Korutinu musíme specificky označit, pokud chceme použít alokátor

```
with_allocator<Alloc, async<T> > coroutine_with_allocator(Alloc &, ...) {

}
```

Takto deklarovaná korutina je kompatibilní s `async<T>`. Jako první parametr se předává instance alokátora. Tento parametr sice propadne i do těla korutiny, ale není potřeba jej nijak zpracovávat. Jeho přítomnost v prvním parametru způsobí, že překladač zakomponuje použití alokátoru pro alokaci korutiny.

**TIP:** Někdy je vhodné deklarovat korutinu s generickým alokátorem Alloc - jako šablonu. Umožňuje to vybrat alokátor až při volání. Pro standardní alokaci slouží alokátor `default_storage`

Volání s alokátorem

```
Alloc allocator(...);
async<T> coro = coroutine_with_allocator(allocator,...);
```

Při používání alokátoru je nutné mít na paměti životnost alokátoru a životnost korutiny. Některé alokátory není třeba držet na živu po tom, co jsou k použity při zavolání korutiny, ale drtivá většina vyžaduje, aby alokátor nebyl ukončen před ukončením korutiny.

### Alokátor: default_storage

Alokuje korutiny standardním způsobem na heapu, slouží pro případ, kdy máme korutinu s povinným alokátorem, ale nemáme po ruce žádný alokátor. Tento alokátor není třeba trvale držet


### Allocator: reusable storage

Alolkátor slouží k alokaci jedné korutiny současně opakovaně. Vhodné použití je v cyklu, kdy se volá stejná korutina

```
template<typename Alloc>
with_allocator<Alloc, async<T> > do_something(Alloc &, int v) {

reusable_storage stor;
for(auto &v : container) {
    co_await do_something(stor, v).start();
}
```

Alokátor najde uplarnění i ve třídách používající korutiny, kde se nepředpokládá, že by metody byly volány paralelně, tedy že v danou chvíli je aktivní pouze jedna instance korutiny

```
class Reader {
public:
    future<int> read_next() {
        return read_next_coro(_storage);
    }

protected:
    reusable_storage _storage;

    with_allocator<reusable_storage, async<int> > read_next_coro() {
        co_await...;
        co_return ..;
    }
};
```

### Allocator: reusable_storage_mtsafe

Funguje stejně jako reusable_storage, ale detekuje násobné použití alokátoru. Pokud je v době použití v alokátoru alokován rámce nějaké korutiny, pak každá další alokace je vyřízena alokací v heapu. Tímto lze použít reusable_storage v místech, kde není jistota, že nedojde k násobnému volání korutiny a přesto k takové sitaci nedochází často.

### Allocator: promise_extra_storage<T, Alloc>

Tento alokátor alokuje extra prostor pro libovolný objekt, který je nějakým způsobem svázaný s korutinou. Je zajištěno, že tento objekt nebude zničen dřív, než rámec dané korutiny

* **T** definuje typ alokovaného objektu
* **Alloc** může specifikovat způsob alokace celého rámce. Zde lze použít default_storage nebo reusable_allocator

Objekt se konstruje předáním továrny (factory), která je zodpovědná za inicializaci T. Ihned po konstrukci korutiny je pak objekt dostupný přes operátor -> nebo *

```
promise_extra_storage<int> storage([]{return 42;});
async<int> coro = do_something_coro(storage,...);
print(*storage);
coro.detach();
```


### Allocator: stack_storage

Tento alokátor obchází problém s nefunkčním coroutine elision v moderních překladačích, kdy překladač není schopen přeskočit alokaci u korutin, které existují v ramci nějaké funkce, tedy přestávají existovat na konci funkce. Tento alokátor umožňuje uložit korutinu na zásobníku, programátor však musí zajistit, že korutina je zničena před opuštěním funkce, jejiž zásobník se použije.

Tento alokátor najde uplatnění zejména v situaci, kdy se část kódu volá opakovaně. Při prvním zavolání totiž nemusí být efektivní

K použití tohoto alokátoru musíme někde deklarovat proměnnou typu std::size_t, která je svázána s korutinou, kterou hodláme volat. Proměnná by měla být staticky alokovaná. Je nutné ji inicializovat na 0. Slouži k uložení informace o tom, jak velký rámce korutina potřebovala (jako že tato informace je dostupná pouze v runtime)

```
//globální stav
static std::size_t coro_sz_state = 0;
//připrav úložiště
stack_storage storage(coro_sz_state);
//alokuj místo na zásobníku
storage = alloca(storage);
//spust korutiny jejíž frame bude v zásobníku
async<int> coro = do_something(storage,...);
```

Objekt `storage` není třeba dále držet, ale je třeba mít na paměti, že alokovaný prostor je rezervovaný jen do konce této funkce. Pokud byl alokovaný prostor příliš malý, pak se korutina alokuje na haldě, ale do sdíleného stavu se uloží požadovaná velikost frame. Při příštím použití stejného sdíleného stavu se bude na zásobníku alokovat prostor této velikosti.

Výhoda tohoto alokátoru je že pokud se podaří rámec spočítat dostatečně velký, přeskočí se veškerá alokace a frame korutiny je umístěno do zásobníku.

